#include "ScriptRunner.h"

#include <chrono>
#include <iostream>
#include <string>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
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

    return passed ? 0 : 1;
}
