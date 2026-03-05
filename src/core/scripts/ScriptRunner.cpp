#include "ScriptRunner.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }

    const int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (requiredSize <= 0) {
        const int acpSize = WideCharToMultiByte(
            CP_ACP,
            0,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (acpSize <= 0) {
            return std::string();
        }

        std::string fallback(static_cast<size_t>(acpSize), '\0');
        WideCharToMultiByte(
            CP_ACP,
            0,
            text.data(),
            static_cast<int>(text.size()),
            fallback.data(),
            acpSize,
            nullptr,
            nullptr
        );
        return fallback;
    }

    std::string result(static_cast<size_t>(requiredSize), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        requiredSize,
        nullptr,
        nullptr
    );
    return result;
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

    const int requiredSize = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (requiredSize <= 0) {
        const int acpSize = MultiByteToWideChar(
            CP_ACP,
            0,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0
        );
        if (acpSize <= 0) {
            return std::wstring();
        }

        std::wstring fallback(static_cast<size_t>(acpSize), L'\0');
        MultiByteToWideChar(
            CP_ACP,
            0,
            text.data(),
            static_cast<int>(text.size()),
            fallback.data(),
            acpSize
        );
        return fallback;
    }

    std::wstring result(static_cast<size_t>(requiredSize), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        requiredSize
    );
    return result;
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
    std::string output;
    HANDLE fileHandle = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return output;
    }
    output = ReadHandleToString(fileHandle);
    CloseHandle(fileHandle);
    return output;
}

bool WriteUtf8File(const std::wstring& filePath, const std::wstring& text) {
    HANDLE fileHandle = CreateFileW(
        filePath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr
    );
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    if (!WriteFile(fileHandle, bom, sizeof(bom), &written, nullptr)) {
        CloseHandle(fileHandle);
        return false;
    }

    const std::string utf8 = WideToUtf8(text);
    if (!utf8.empty()) {
        written = 0;
        if (!WriteFile(fileHandle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
            CloseHandle(fileHandle);
            return false;
        }
    }

    CloseHandle(fileHandle);
    return true;
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
            *errorText = L"Не удалось создать stdin pipe.";
        }
        return false;
    }

    if (!SetHandleInformation(childStdInWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        if (errorText) {
            *errorText = L"Не удалось настроить stdin pipe.";
        }
        return false;
    }

    if (!CreatePipe(&childStdOutRead, &childStdOutWrite, &sa, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        if (errorText) {
            *errorText = L"Не удалось создать stdout pipe.";
        }
        return false;
    }

    if (!SetHandleInformation(childStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        closeHandle(childStdInRead);
        closeHandle(childStdInWrite);
        closeHandle(childStdOutRead);
        closeHandle(childStdOutWrite);
        if (errorText) {
            *errorText = L"Не удалось настроить stdout pipe.";
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
            *errorText = L"Не удалось получить каталог временных файлов.";
        }
        return false;
    }

    if (!GetTempFileNameW(tempDirectory, L"tmg", 0, stderrTempFilePath)) {
        closeHandle(childStdInRead);
        closeHandle(childStdInWrite);
        closeHandle(childStdOutRead);
        closeHandle(childStdOutWrite);
        if (errorText) {
            *errorText = L"Не удалось создать временный файл stderr.";
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
            *errorText = L"Не удалось открыть временный файл stderr.";
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
            *errorText = L"CreateProcessW failed: " + std::to_wstring(GetLastError());
        }
        return false;
    }

    const std::string utf8Input = WideToUtf8(inputText);
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
                    *errorText = L"WriteFile failed: " + std::to_wstring(writeError);
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
            *errorText = L"Скрипт не завершился за 30 секунд.";
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
            *errorText = L"Код выхода " + std::to_wstring(exitCode);
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
            *errorText = L"Тело скрипта пустое.";
        }
        return false;
    }

    wchar_t tempDirectory[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDirectory)) {
        if (errorText) {
            *errorText = L"Не удалось получить каталог временных файлов.";
        }
        return false;
    }

    wchar_t tempFilePath[MAX_PATH] = {};
    if (!GetTempFileNameW(tempDirectory, L"tmg", 0, tempFilePath)) {
        if (errorText) {
            *errorText = L"Не удалось создать временный файл скрипта.";
        }
        return false;
    }

    std::wstring scriptPath = tempFilePath;
    const size_t dotPos = scriptPath.find_last_of(L'.');
    if (dotPos != std::wstring::npos) {
        scriptPath = scriptPath.substr(0, dotPos);
    }
    scriptPath += L".ps1";

    if (!MoveFileExW(tempFilePath, scriptPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tempFilePath);
        if (errorText) {
            *errorText = L"Не удалось подготовить временный .ps1 файл: " + std::to_wstring(GetLastError());
        }
        return false;
    }

    if (!WriteUtf8File(scriptPath, scriptBody)) {
        DeleteFileW(scriptPath.c_str());
        if (errorText) {
            *errorText = L"Не удалось записать временный скрипт.";
        }
        return false;
    }

    const std::wstring commandLine =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + scriptPath + L"\"";

    const bool executeOk = Execute(commandLine, inputText, outputText, errorText);
    DeleteFileW(scriptPath.c_str());
    return executeOk;
}
