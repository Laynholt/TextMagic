#pragma once

#include <string>

class ScriptRunner {
public:
    bool Execute(
        const std::wstring& commandLine,
        const std::wstring& inputText,
        std::wstring* outputText,
        std::wstring* errorText
    ) const;

    bool ExecutePowerShellScript(
        const std::wstring& scriptBody,
        const std::wstring& inputText,
        std::wstring* outputText,
        std::wstring* errorText
    ) const;
};
