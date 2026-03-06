#pragma once

#include <string>
#include <string_view>

namespace PowerShellUtils {
const wchar_t* GetExecutableName();
std::wstring EscapeSingleQuoted(std::wstring_view text);
} // namespace PowerShellUtils
