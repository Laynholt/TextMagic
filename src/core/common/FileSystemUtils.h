#pragma once

#include <windows.h>

#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

namespace FileSystemUtils {

bool CollectRegularFiles(
    const std::filesystem::path& root,
    bool recursive,
    std::vector<std::filesystem::path>* files,
    std::error_code* error);

std::wstring GetTempDirectory(std::error_code* error);
std::wstring GetModulePath(HMODULE module, std::error_code* error);

} // namespace FileSystemUtils
