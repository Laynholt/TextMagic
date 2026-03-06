#pragma once

#include <string>

namespace EncodingUtils {
std::string WideToUtf8(const std::wstring& text, bool allowAnsiFallback = true);
std::wstring Utf8ToWide(const std::string& text, bool strictUtf8 = false, bool allowAnsiFallback = true);
} // namespace EncodingUtils
