#include "ScriptRunner.h"
#include "ScriptManifest.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

const ScriptManifest::Entry* FindByName(
    const std::vector<ScriptManifest::Entry>& entries,
    const wchar_t* name
) {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

bool CheckConversion(
    const ScriptRunner& runner,
    const std::wstring& script,
    const std::wstring& input,
    const std::wstring& expected
) {
    std::wstring output;
    std::wstring error;
    const bool ok = runner.ExecutePowerShellScript(script, input, &output, &error);
    return Check(ok, "layout conversion executes")
        & Check(error.empty(), "layout conversion reports no error")
        & Check(output == expected, "layout conversion preserves case");
}

std::wstring MakeStreamScript(const wchar_t* streamName, std::uint64_t byteCount) {
    constexpr std::uint64_t kChunkBytes = 1024ull * 1024ull;
    return
        L"$stream=[Console]::" + std::wstring(streamName) + L"\n"
        L"$buffer=[Text.Encoding]::ASCII.GetBytes(('A' * " + std::to_wstring(kChunkBytes) + L"))\n"
        L"$remaining=" + std::to_wstring(byteCount) + L"\n"
        L"while($remaining -gt 0){\n"
        L"  $count=[Math]::Min($remaining,$buffer.Length)\n"
        L"  $stream.Write($buffer,0,$count)\n"
        L"  $remaining-=$count\n"
        L"}\n";
}

bool CheckStreamBoundary(
    const ScriptRunner& runner,
    const wchar_t* streamName,
    const char* exactMessage,
    const char* oversizedMessage
) {
    constexpr std::uint64_t kLimitBytes = 16ull * 1024ull * 1024ull;
    std::wstring error;
    const bool exactOk = runner.ExecutePowerShellScript(
        MakeStreamScript(streamName, kLimitBytes),
        L"",
        nullptr,
        &error,
        30000
    );
    bool passed = Check(exactOk, exactMessage)
        & Check(error.empty(), "exact-limit stream reports no error");

    error.clear();
    const bool oversizedOk = runner.ExecutePowerShellScript(
        MakeStreamScript(streamName, kLimitBytes + 1),
        L"",
        nullptr,
        &error,
        30000
    );
    passed &= Check(!oversizedOk, oversizedMessage)
        & Check(!error.empty(), "oversized stream reports an output-limit error");
    return passed;
}

class ObservedChild {
public:
    ~ObservedChild() {
        if (m_handle) {
            if (WaitForSingleObject(m_handle, 0) != WAIT_OBJECT_0) {
                TerminateProcess(m_handle, 1);
                WaitForSingleObject(m_handle, 5000);
            }
            CloseHandle(m_handle);
        }
    }

    bool OpenFromFile(const wchar_t* pidPath) {
        constexpr DWORD kPidObservationMs = 2000;
        const auto deadline = GetTickCount64() + kPidObservationMs;
        do {
            std::wifstream pidFile{std::filesystem::path(pidPath)};
            pidFile >> m_pid;
            if (m_pid != 0) {
                m_handle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, m_pid);
                if (m_handle || GetLastError() == ERROR_INVALID_PARAMETER) {
                    return true;
                }
            }
            Sleep(10);
        } while (GetTickCount64() < deadline);
        return false;
    }

    DWORD Pid() const noexcept {
        return m_pid;
    }

    bool HasExited() const {
        return !m_handle || WaitForSingleObject(m_handle, 0) == WAIT_OBJECT_0;
    }

private:
    DWORD m_pid = 0;
    HANDLE m_handle = nullptr;
};

bool CreatePidPath(wchar_t* pidPath) {
    wchar_t tempDirectory[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDirectory)
        || !GetTempFileNameW(tempDirectory, L"tmg", 0, pidPath)) {
        return Check(false, "child PID temp file is created");
    }
    DeleteFileW(pidPath);
    return true;
}

std::wstring EscapePowerShellLiteral(const wchar_t* value) {
    std::wstring escaped(value);
    size_t quote = 0;
    while ((quote = escaped.find(L'\'', quote)) != std::wstring::npos) {
        escaped.insert(quote, 1, L'\'');
        quote += 2;
    }
    return escaped;
}

bool CheckTimeoutKillsChild(const ScriptRunner& runner) {
    wchar_t pidPath[MAX_PATH] = {};
    if (!CreatePidPath(pidPath)) {
        return false;
    }

    const std::wstring escapedPidPath = EscapePowerShellLiteral(pidPath);
    const std::wstring script =
        L"$child=Start-Process -FilePath 'cmd.exe' "
        L"-ArgumentList '/d','/c','ping -n 31 127.0.0.1 > nul' -PassThru\n"
        L"[IO.File]::WriteAllText('" + escapedPidPath + L"',$child.Id.ToString())\n"
        L"Start-Sleep -Seconds 30\n";

    std::wstring error;
    const bool runOk = runner.ExecutePowerShellScript(script, L"", nullptr, &error, 2000);

    ObservedChild child;
    const bool childObserved = child.OpenFromFile(pidPath);
    DeleteFileW(pidPath);

    const bool passed = Check(!runOk, "parent script times out")
        & Check(childObserved && child.Pid() != 0, "timed-out script records its child PID")
        & Check(child.HasExited(), "timed-out script leaves no live child process");
    return passed;
}

bool CheckSuccessfulParentKillsChild(const ScriptRunner& runner) {
    wchar_t pidPath[MAX_PATH] = {};
    if (!CreatePidPath(pidPath)) {
        return false;
    }

    const std::wstring script =
        L"$child=Start-Process -FilePath 'cmd.exe' "
        L"-ArgumentList '/d','/c','ping -n 31 127.0.0.1 > nul & echo child' -PassThru\n"
        L"[IO.File]::WriteAllText('" + EscapePowerShellLiteral(pidPath) + L"',$child.Id.ToString())\n"
        L"[Console]::Out.Write('parent')\n";

    std::wstring output;
    std::wstring error;
    const bool runOk = runner.ExecutePowerShellScript(script, L"", &output, &error, 10000);

    ObservedChild child;
    const bool childObserved = child.OpenFromFile(pidPath);
    DeleteFileW(pidPath);

    return Check(runOk, "successful parent script succeeds")
        & Check(error.empty(), "successful parent reports no error")
        & Check(output == L"parent", "successful parent output is captured completely")
        & Check(childObserved && child.Pid() != 0, "successful parent records its child PID")
        & Check(child.HasExited(), "successful parent leaves no live child process");
}
}

int main() {
    ScriptRunner runner;
    std::wstring output;
    std::wstring error;
    const std::wstring script =
        L"[Console]::InputEncoding=[Text.Encoding]::UTF8\n"
        L"[Console]::OutputEncoding=[Text.Encoding]::UTF8\n"
        L"$value=[Console]::In.ReadToEnd()\n"
        L"[Console]::Out.Write($value)\n";

    const bool ok = runner.ExecutePowerShellScript(
        script,
        L"Hello, РџСЂРёРІРµС‚",
        &output,
        &error
    );
    bool passed =
        Check(ok, "PowerShell script executes")
        & Check(error.empty(), "PowerShell script reports no error")
        & Check(output == L"Hello, РџСЂРёРІРµС‚", "PowerShell script preserves UTF-8 input");
    output.clear();
    error.clear();
    const bool warningOk = runner.ExecutePowerShellScript(
        L"[Console]::Out.Write('ok')\n[Console]::Error.Write('warning')\n",
        L"",
        &output,
        &error
    );
    passed &=
        Check(warningOk, "stderr does not fail a successful process")
        & Check(error.empty(), "successful process reports no error")
        & Check(output == L"ok", "successful process preserves stdout");

    output.clear();
    error.clear();
    const auto timeoutStart = std::chrono::steady_clock::now();
    const bool timeoutOk = runner.ExecutePowerShellScript(
        L"Start-Sleep -Seconds 5\n[Console]::Out.Write('late')\n",
        L"",
        &output,
        &error,
        100
    );
    const auto timeoutElapsed = std::chrono::steady_clock::now() - timeoutStart;
    passed &=
        Check(!timeoutOk, "timed-out process fails")
        & Check(timeoutElapsed < std::chrono::seconds(2), "timeout is enforced before stdout closes");

    passed &= CheckStreamBoundary(
        runner,
        L"OpenStandardOutput()",
        "stdout at exactly 16 MiB succeeds",
        "stdout above 16 MiB is rejected"
    );
    passed &= CheckStreamBoundary(
        runner,
        L"OpenStandardError()",
        "stderr at exactly 16 MiB succeeds",
        "stderr above 16 MiB is rejected"
    );
    passed &= CheckTimeoutKillsChild(runner);
    passed &= CheckSuccessfulParentKillsChild(runner);

    const ScriptManifest::LoadResult manifests = ScriptManifest::LoadFromDirectory(
        (std::filesystem::path(TEXTMAGIC_SOURCE_DIR) / L"scripts").wstring());
    const auto* layoutScript = FindByName(manifests.entries, L"Layout Auto QWERTY");
    passed &= Check(layoutScript != nullptr, "bundled layout script is loaded");
    if (layoutScript) {
        passed &= CheckConversion(runner, layoutScript->scriptBody, L"ghbdtn", L"\u043F\u0440\u0438\u0432\u0435\u0442");
        passed &= CheckConversion(runner, layoutScript->scriptBody, L"Ghbdtn", L"\u041F\u0440\u0438\u0432\u0435\u0442");
        passed &= CheckConversion(runner, layoutScript->scriptBody, L"GHBDTN", L"\u041F\u0420\u0418\u0412\u0415\u0422");
        passed &= CheckConversion(runner, layoutScript->scriptBody, L"\u043F\u0440\u0438\u0432\u0435\u0442", L"ghbdtn");
        passed &= CheckConversion(runner, layoutScript->scriptBody, L"\u041F\u0440\u0438\u0432\u0435\u0442", L"Ghbdtn");
        passed &= CheckConversion(runner, layoutScript->scriptBody, L"\u041F\u0420\u0418\u0412\u0415\u0422", L"GHBDTN");
    }

    return passed ? 0 : 1;
}
