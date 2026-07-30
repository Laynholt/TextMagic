#pragma once

#include <string>
#include <string_view>

namespace PowerShellUtils {
std::wstring EscapeSingleQuoted(std::wstring_view text);
} // namespace PowerShellUtils
