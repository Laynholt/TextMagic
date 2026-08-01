#pragma once

#include <string>

namespace LogFile {
bool Append(const std::wstring& path, const std::wstring& line);
std::wstring Read(const std::wstring& path);
bool Clear(const std::wstring& path);
}
