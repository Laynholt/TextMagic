#include "PowerShellUtils.h"

namespace PowerShellUtils {
const wchar_t* GetExecutableName() {
    return L"powershell.exe";
}

std::wstring EscapeSingleQuoted(std::wstring_view text) {
    std::wstring escaped;
    escaped.reserve(text.size());

    for (wchar_t ch : text) {
        if (ch == L'\'') {
            escaped += L"''";
        } else {
            escaped.push_back(ch);
        }
    }

    return escaped;
}
} // namespace PowerShellUtils
