#include "ScriptRunner.h"
#include "ScriptManifest.h"

#include <chrono>
#include <filesystem>
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
