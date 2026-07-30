#include "ScriptRunner.h"

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
    const bool passed =
        Check(ok, "PowerShell script executes")
        & Check(error.empty(), "PowerShell script reports no error")
        & Check(output == L"Hello, РџСЂРёРІРµС‚", "PowerShell script preserves UTF-8 input");
    return passed ? 0 : 1;
}
