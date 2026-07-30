#pragma once

#include <string>

namespace EncodingUtils {
std::string WideToUtf8(const std::wstring& text);
std::wstring Utf8ToWide(const std::string& text, bool strictUtf8 = false, bool allowAnsiFallback = true);
} // namespace EncodingUtils
