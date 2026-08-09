#include "ScriptRunner.h"

#include "EncodingUtils.h"
#include "Localization.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
const wchar_t* T(const wchar_t* key) {
    return Localization::GetTextByName(key);
}

std::wstring BytesToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }

    // PowerShell often writes redirected stderr as UTF-16LE text.
    size_t zeroBytes = 0;
    for (char ch : text) {
        if (ch == '\0') {
            ++zeroBytes;
        }
    }
    if (zeroBytes > (text.size() / 4) && text.size() >= 2) {
        size_t start = 0;
        if (static_cast<unsigned char>(text[0]) == 0xFF && static_cast<unsigned char>(text[1]) == 0xFE) {
            start = 2;
        }
        const size_t wcharCount = (text.size() - start) / sizeof(wchar_t);
        if (wcharCount > 0) {
            std::wstring wide(wcharCount, L'\0');
            memcpy(wide.data(), text.data() + start, wcharCount * sizeof(wchar_t));
            while (!wide.empty() && wide.back() == L'\0') {
                wide.pop_back();
            }
            return wide;
        }
    }

    return EncodingUtils::Utf8ToWide(text);
}

std::string ReadFileToString(const std::wstring& filePath) {
    std::ifstream input(std::filesystem::path(filePath), std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

std::wstring Base64Encode(const unsigned char* data, size_t size) {
    if (!data || size == 0 || size > MAXDWORD) {
        return {};
    }

    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    DWORD outputLength = 0;
    if (!CryptBinaryToStringW(
            data,
            static_cast<DWORD>(size),
            flags,
            nullptr,
            &outputLength)) {
        return {};
    }

    std::wstring encoded(outputLength, L'\0');
    if (!CryptBinaryToStringW(
            data,
            static_cast<DWORD>(size),
            flags,
            encoded.data(),
            &outputLength)) {
        return {};
    }
    if (!encoded.empty() && encoded.back() == L'\0') {
        encoded.pop_back();
    }
    return encoded;
}
}

bool ScriptRunner::Execute(
    const std::wstring& commandLine,
    const std::wstring& inputText,
    std::wstring* outputText,
    std::wstring* errorText,
    unsigned long timeoutMs
) const {
    if (outputText) {
        outputText->clear();
    }
    if (errorText) {
        errorText->clear();
    }

    auto closeHandle = [](HANDLE& handle) {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = nullptr;
        }
    };

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdIn = nullptr;
    HANDLE childStdOut = nullptr;
    HANDLE childStdErr = nullptr;
    wchar_t stdinTempFilePath[MAX_PATH] = {};
    wchar_t stdoutTempFilePath[MAX_PATH] = {};
    wchar_t stderrTempFilePath[MAX_PATH] = {};

    wchar_t tempDirectory[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDirectory)) {
        if (errorText) {
            *errorText = T(L"script_runner.error.temp_dir");
        }
        return false;
    }

    auto createTempFile = [&](wchar_t* path) -> HANDLE {
        if (!GetTempFileNameW(tempDirectory, L"tmg", 0, path)) {
            return nullptr;
        }
        HANDLE handle = CreateFileW(
            path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            &sa,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY,
            nullptr
        );
        if (handle == INVALID_HANDLE_VALUE) {
            DeleteFileW(path);
            path[0] = L'\0';
            return nullptr;
        }
        return handle;
    };

    auto cleanupTempFile = [&](HANDLE& handle, wchar_t* path) {
        closeHandle(handle);
        if (path[0] != L'\0') {
            DeleteFileW(path);
            path[0] = L'\0';
        }
    };

    childStdIn = createTempFile(stdinTempFilePath);
    childStdOut = createTempFile(stdoutTempFilePath);
    childStdErr = createTempFile(stderrTempFilePath);
    if (!childStdIn || !childStdOut || !childStdErr) {
        cleanupTempFile(childStdIn, stdinTempFilePath);
        cleanupTempFile(childStdOut, stdoutTempFilePath);
        cleanupTempFile(childStdErr, stderrTempFilePath);
        if (errorText) {
            *errorText = T(L"script_runner.error.stderr_temp_create");
        }
        return false;
    }

    const std::string utf8Input = EncodingUtils::WideToUtf8(inputText);
    DWORD writtenBytes = 0;
    const BOOL wroteInput = utf8Input.empty() || WriteFile(
        childStdIn,
        utf8Input.data(),
        static_cast<DWORD>(utf8Input.size()),
        &writtenBytes,
        nullptr
    );
    LARGE_INTEGER fileStart = {};
    if (!wroteInput || writtenBytes != utf8Input.size()
        || !SetFilePointerEx(childStdIn, fileStart, nullptr, FILE_BEGIN)) {
        const DWORD writeError = GetLastError();
        cleanupTempFile(childStdIn, stdinTempFilePath);
        cleanupTempFile(childStdOut, stdoutTempFilePath);
        cleanupTempFile(childStdErr, stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.write_file_prefix")) + std::to_wstring(writeError);
        }
        return false;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdIn;
    si.hStdOutput = childStdOut;
    si.hStdError = childStdErr;

    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    const BOOL started = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    closeHandle(childStdIn);
    closeHandle(childStdOut);
    closeHandle(childStdErr);

    if (!started) {
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.create_process_prefix")) + std::to_wstring(GetLastError());
        }
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = T(L"script_runner.error.timeout");
        }
        return false;
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    closeHandle(pi.hThread);
    closeHandle(pi.hProcess);

    const std::string utf8Output = ReadFileToString(stdoutTempFilePath);
    const std::string stderrBytes = ReadFileToString(stderrTempFilePath);
    DeleteFileW(stdinTempFilePath);
    DeleteFileW(stdoutTempFilePath);
    DeleteFileW(stderrTempFilePath);

    const std::wstring output = BytesToWide(utf8Output);
    const std::wstring stderrText = BytesToWide(stderrBytes);
    if (outputText) {
        *outputText = output;
    }

    if (exitCode != 0) {
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.exit_code_prefix")) + std::to_wstring(exitCode);
            if (!output.empty()) {
                *errorText += L"; stdout: " + output;
            }
            if (!stderrText.empty()) {
                *errorText += L"; stderr: " + stderrText;
            }
        }
        return false;
    }

    return true;
}

bool ScriptRunner::ExecutePowerShellScript(
    const std::wstring& scriptBody,
    const std::wstring& inputText,
    std::wstring* outputText,
    std::wstring* errorText,
    unsigned long timeoutMs
) const {
    if (outputText) {
        outputText->clear();
    }
    if (errorText) {
        errorText->clear();
    }

    if (scriptBody.empty()) {
        if (errorText) {
            *errorText = T(L"script_runner.error.empty_body");
        }
        return false;
    }

    const unsigned char* scriptBytes = reinterpret_cast<const unsigned char*>(scriptBody.data());
    const size_t scriptBytesCount = scriptBody.size() * sizeof(wchar_t);
    const std::wstring encodedCommand = Base64Encode(scriptBytes, scriptBytesCount);
    if (encodedCommand.empty()) {
        if (errorText) {
            *errorText = T(L"script_runner.error.script_temp_write");
        }
        return false;
    }

    const std::wstring commandLine =
        std::wstring(L"powershell.exe")
        + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand "
        + encodedCommand;

    return Execute(commandLine, inputText, outputText, errorText, timeoutMs);
}
