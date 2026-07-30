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

std::string ReadHandleToString(HANDLE handle) {
    std::string output;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return output;
    }

    char buffer[4096];
    while (true) {
        DWORD readBytes = 0;
        const BOOL readOk = ReadFile(handle, buffer, sizeof(buffer), &readBytes, nullptr);
        if (!readOk || readBytes == 0) {
            break;
        }
        output.append(buffer, readBytes);
    }
    return output;
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
    std::wstring* errorText
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

    HANDLE childStdInRead = nullptr;
    HANDLE childStdInWrite = nullptr;
    HANDLE childStdOutRead = nullptr;
    HANDLE childStdOutWrite = nullptr;
    HANDLE childStdErrWrite = nullptr;
    wchar_t stderrTempFilePath[MAX_PATH] = {};

    if (!CreatePipe(&childStdInRead, &childStdInWrite, &sa, 0)) {
        if (errorText) {
            *errorText = T(L"script_runner.error.stdin_create");
        }
        return false;
    }

    if (!SetHandleInformation(childStdInWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        if (errorText) {
            *errorText = T(L"script_runner.error.stdin_config");
        }
        return false;
    }

    if (!CreatePipe(&childStdOutRead, &childStdOutWrite, &sa, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        if (errorText) {
            *errorText = T(L"script_runner.error.stdout_create");
        }
        return false;
    }

    if (!SetHandleInformation(childStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        closeHandle(childStdInRead);
        closeHandle(childStdInWrite);
        closeHandle(childStdOutRead);
        closeHandle(childStdOutWrite);
        if (errorText) {
            *errorText = T(L"script_runner.error.stdout_config");
        }
        return false;
    }

    wchar_t tempDirectory[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDirectory)) {
        closeHandle(childStdInRead);
        closeHandle(childStdInWrite);
        closeHandle(childStdOutRead);
        closeHandle(childStdOutWrite);
        if (errorText) {
            *errorText = T(L"script_runner.error.temp_dir");
        }
        return false;
    }

    if (!GetTempFileNameW(tempDirectory, L"tmg", 0, stderrTempFilePath)) {
        closeHandle(childStdInRead);
        closeHandle(childStdInWrite);
        closeHandle(childStdOutRead);
        closeHandle(childStdOutWrite);
        if (errorText) {
            *errorText = T(L"script_runner.error.stderr_temp_create");
        }
        return false;
    }

    childStdErrWrite = CreateFileW(
        stderrTempFilePath,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr
    );
    if (childStdErrWrite == INVALID_HANDLE_VALUE) {
        childStdErrWrite = nullptr;
        closeHandle(childStdInRead);
        closeHandle(childStdInWrite);
        closeHandle(childStdOutRead);
        closeHandle(childStdOutWrite);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = T(L"script_runner.error.stderr_temp_open");
        }
        return false;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdInRead;
    si.hStdOutput = childStdOutWrite;
    si.hStdError = childStdErrWrite;

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

    closeHandle(childStdInRead);
    closeHandle(childStdOutWrite);
    closeHandle(childStdErrWrite);

    if (!started) {
        closeHandle(childStdInWrite);
        closeHandle(childStdOutRead);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.create_process_prefix")) + std::to_wstring(GetLastError());
        }
        return false;
    }

    const std::string utf8Input = EncodingUtils::WideToUtf8(inputText);
    size_t sentBytes = 0;
    while (sentBytes < utf8Input.size()) {
        DWORD writtenBytes = 0;
        const DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(32768, utf8Input.size() - sentBytes));
        const BOOL wrote = WriteFile(
            childStdInWrite,
            utf8Input.data() + sentBytes,
            chunkSize,
            &writtenBytes,
            nullptr
        );

        if (!wrote) {
            const DWORD writeError = GetLastError();
            if (writeError != ERROR_BROKEN_PIPE) {
                closeHandle(childStdInWrite);
                closeHandle(childStdOutRead);
                TerminateProcess(pi.hProcess, 1);
                closeHandle(pi.hThread);
                closeHandle(pi.hProcess);
                DeleteFileW(stderrTempFilePath);
                if (errorText) {
                    *errorText = std::wstring(T(L"script_runner.error.write_file_prefix")) + std::to_wstring(writeError);
                }
                return false;
            }
            break;
        }

        if (writtenBytes == 0) {
            break;
        }

        sentBytes += writtenBytes;
    }

    closeHandle(childStdInWrite);

    const std::string utf8Output = ReadHandleToString(childStdOutRead);

    closeHandle(childStdOutRead);

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
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

    const std::string stderrBytes = ReadFileToString(stderrTempFilePath);
    DeleteFileW(stderrTempFilePath);

    const std::wstring output = BytesToWide(utf8Output);
    const std::wstring stderrText = BytesToWide(stderrBytes);
    if (outputText) {
        *outputText = output;
    }

    if (exitCode != 0 || !stderrText.empty()) {
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
    std::wstring* errorText
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

    return Execute(commandLine, inputText, outputText, errorText);
}
