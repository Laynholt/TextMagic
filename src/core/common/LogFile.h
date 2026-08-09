#pragma once

#include <cstdint>
#include <string>

namespace LogFile {
bool Append(const std::wstring& path,
            const std::wstring& line,
            std::uintmax_t maxBytes = 1024 * 1024);
std::wstring Read(const std::wstring& path);
bool Clear(const std::wstring& path);
}
