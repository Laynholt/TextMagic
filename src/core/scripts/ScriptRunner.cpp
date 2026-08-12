#include "ScriptRunner.h"

#include "EncodingUtils.h"
#include "Localization.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr std::uint64_t kMaxCapturedStreamBytes = 16ull * 1024ull * 1024ull;
constexpr DWORD kTerminationWaitMs = 5000;
constexpr ULONG_PTR kJobCompletionKey = 1;

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

bool ReadFileToString(const std::wstring& filePath, std::string* text, DWORD* error) {
    if (!text || !error) {
        return false;
    }
    text->clear();
    *error = ERROR_SUCCESS;

    HANDLE file = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size)) {
        *error = GetLastError();
        CloseHandle(file);
        return false;
    }
    if (size.QuadPart < 0
        || static_cast<std::uint64_t>(size.QuadPart) > kMaxCapturedStreamBytes) {
        *error = ERROR_FILE_TOO_LARGE;
        CloseHandle(file);
        return false;
    }

    text->resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < text->size()) {
        const DWORD requested = static_cast<DWORD>(std::min<size_t>(
            text->size() - offset,
            MAXDWORD
        ));
        DWORD bytesRead = 0;
        if (!ReadFile(file, text->data() + offset, requested, &bytesRead, nullptr)) {
            *error = GetLastError();
            text->clear();
            CloseHandle(file);
            return false;
        }
        if (bytesRead == 0) {
            *error = ERROR_HANDLE_EOF;
            text->clear();
            CloseHandle(file);
            return false;
        }
        offset += bytesRead;
    }
    CloseHandle(file);
    return true;
}

bool WaitForJobEmpty(HANDLE completionPort, DWORD timeoutMs, DWORD* error) {
    if (!completionPort || !error) {
        return false;
    }
    *error = ERROR_SUCCESS;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = now >= deadline
            ? 0
            : static_cast<DWORD>(deadline - now);
        DWORD message = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED messageValue = nullptr;
        const BOOL dequeued = GetQueuedCompletionStatus(
            completionPort,
            &message,
            &completionKey,
            &messageValue,
            remaining
        );
        if (!dequeued) {
            *error = GetLastError();
            return false;
        }
        if (completionKey == kJobCompletionKey
            && message == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) {
            return true;
        }
    }
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
    if (utf8Input.size() > MAXDWORD) {
        cleanupTempFile(childStdIn, stdinTempFilePath);
        cleanupTempFile(childStdOut, stdoutTempFilePath);
        cleanupTempFile(childStdErr, stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.write_file_prefix"))
                + std::to_wstring(ERROR_FILE_TOO_LARGE);
        }
        return false;
    }
    DWORD writtenBytes = 0;
    const BOOL wroteInput = utf8Input.empty() || WriteFile(
        childStdIn,
        utf8Input.data(),
        static_cast<DWORD>(utf8Input.size()),
        &writtenBytes,
        nullptr
    );
    LARGE_INTEGER fileStart = {};
    if (!wroteInput || writtenBytes != static_cast<DWORD>(utf8Input.size())
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
        CREATE_SUSPENDED | CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!started) {
        const DWORD createError = GetLastError();
        closeHandle(childStdIn);
        closeHandle(childStdOut);
        closeHandle(childStdErr);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.create_process_prefix")) + std::to_wstring(createError);
        }
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        const DWORD jobError = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, kTerminationWaitMs);
        closeHandle(childStdIn);
        closeHandle(childStdOut);
        closeHandle(childStdErr);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.create_job_prefix")) + std::to_wstring(jobError);
        }
        return false;
    }

    HANDLE completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!completionPort) {
        const DWORD completionPortError = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, kTerminationWaitMs);
        closeHandle(childStdIn);
        closeHandle(childStdOut);
        closeHandle(childStdErr);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.create_completion_port_prefix"))
                + std::to_wstring(completionPortError);
        }
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        const DWORD jobError = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, kTerminationWaitMs);
        closeHandle(childStdIn);
        closeHandle(childStdOut);
        closeHandle(childStdErr);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.configure_job_prefix")) + std::to_wstring(jobError);
        }
        return false;
    }

    JOBOBJECT_ASSOCIATE_COMPLETION_PORT completionPortAssociation = {};
    completionPortAssociation.CompletionKey = reinterpret_cast<PVOID>(kJobCompletionKey);
    completionPortAssociation.CompletionPort = completionPort;
    if (!SetInformationJobObject(
            job,
            JobObjectAssociateCompletionPortInformation,
            &completionPortAssociation,
            sizeof(completionPortAssociation))) {
        const DWORD associationError = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, kTerminationWaitMs);
        closeHandle(childStdIn);
        closeHandle(childStdOut);
        closeHandle(childStdErr);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.associate_completion_port_prefix"))
                + std::to_wstring(associationError);
        }
        return false;
    }

    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        const DWORD jobError = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, kTerminationWaitMs);
        closeHandle(childStdIn);
        closeHandle(childStdOut);
        closeHandle(childStdErr);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.assign_job_prefix")) + std::to_wstring(jobError);
        }
        return false;
    }

    if (ResumeThread(pi.hThread) == DWORD(-1)) {
        const DWORD resumeError = GetLastError();
        TerminateJobObject(job, 1);
        DWORD ignoredDrainError = ERROR_SUCCESS;
        WaitForJobEmpty(completionPort, kTerminationWaitMs, &ignoredDrainError);
        closeHandle(childStdIn);
        closeHandle(childStdOut);
        closeHandle(childStdErr);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.resume_thread_prefix")) + std::to_wstring(resumeError);
        }
        return false;
    }

    closeHandle(childStdIn);
    closeHandle(childStdOut);
    closeHandle(childStdErr);

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        DWORD terminationError = ERROR_SUCCESS;
        if (!TerminateJobObject(job, 1)) {
            terminationError = GetLastError();
        }
        DWORD drainError = ERROR_SUCCESS;
        if (terminationError == ERROR_SUCCESS) {
            WaitForJobEmpty(completionPort, kTerminationWaitMs, &drainError);
        }
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            if (terminationError != ERROR_SUCCESS) {
                *errorText = std::wstring(T(L"script_runner.error.terminate_job_prefix"))
                    + std::to_wstring(terminationError);
            } else if (drainError != ERROR_SUCCESS) {
                *errorText = std::wstring(T(L"script_runner.error.drain_job_prefix"))
                    + std::to_wstring(drainError);
            } else {
                *errorText = T(L"script_runner.error.timeout");
            }
        }
        return false;
    }

    if (waitResult != WAIT_OBJECT_0) {
        const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA;
        TerminateJobObject(job, 1);
        DWORD ignoredDrainError = ERROR_SUCCESS;
        WaitForJobEmpty(completionPort, kTerminationWaitMs, &ignoredDrainError);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.wait_process_prefix")) + std::to_wstring(waitError);
        }
        return false;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
        const DWORD exitCodeError = GetLastError();
        TerminateJobObject(job, 1);
        DWORD ignoredDrainError = ERROR_SUCCESS;
        WaitForJobEmpty(completionPort, kTerminationWaitMs, &ignoredDrainError);
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            *errorText = std::wstring(T(L"script_runner.error.get_exit_code_prefix"))
                + std::to_wstring(exitCodeError);
        }
        return false;
    }

    DWORD terminationError = ERROR_SUCCESS;
    if (!TerminateJobObject(job, exitCode)) {
        terminationError = GetLastError();
    }
    DWORD drainError = ERROR_SUCCESS;
    if (terminationError == ERROR_SUCCESS) {
        WaitForJobEmpty(completionPort, kTerminationWaitMs, &drainError);
    }
    if (terminationError != ERROR_SUCCESS || drainError != ERROR_SUCCESS) {
        closeHandle(pi.hThread);
        closeHandle(pi.hProcess);
        closeHandle(job);
        closeHandle(completionPort);
        DeleteFileW(stdinTempFilePath);
        DeleteFileW(stdoutTempFilePath);
        DeleteFileW(stderrTempFilePath);
        if (errorText) {
            if (terminationError != ERROR_SUCCESS) {
                *errorText = std::wstring(T(L"script_runner.error.terminate_job_prefix"))
                    + std::to_wstring(terminationError);
            } else {
                *errorText = std::wstring(T(L"script_runner.error.drain_job_prefix"))
                    + std::to_wstring(drainError);
            }
        }
        return false;
    }
    closeHandle(pi.hThread);
    closeHandle(pi.hProcess);
    closeHandle(job);
    closeHandle(completionPort);

    std::string utf8Output;
    std::string stderrBytes;
    DWORD stdoutError = ERROR_SUCCESS;
    DWORD stderrError = ERROR_SUCCESS;
    const bool stdoutRead = ReadFileToString(stdoutTempFilePath, &utf8Output, &stdoutError);
    const bool stderrRead = ReadFileToString(stderrTempFilePath, &stderrBytes, &stderrError);
    DeleteFileW(stdinTempFilePath);
    DeleteFileW(stdoutTempFilePath);
    DeleteFileW(stderrTempFilePath);

    if (!stdoutRead || !stderrRead) {
        const DWORD readError = !stdoutRead ? stdoutError : stderrError;
        if (errorText) {
            if (readError == ERROR_FILE_TOO_LARGE) {
                *errorText = T(L"script_runner.error.output_limit");
            } else {
                *errorText = std::wstring(T(L"script_runner.error.read_output_prefix"))
                    + std::to_wstring(readError);
            }
        }
        return false;
    }

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
