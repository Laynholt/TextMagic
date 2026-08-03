#include "ApplicationBlacklist.h"

#include "EncodingUtils.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {
void SetError(std::wstring* errorMessage, const wchar_t* operation) {
    if (errorMessage) {
        *errorMessage = std::wstring(operation) + L" (" + std::to_wstring(GetLastError()) + L")";
    }
}

std::wstring Trim(const std::wstring& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t character) {
        return std::iswspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character) {
        return std::iswspace(character) != 0;
    }).base();
    return first < last ? std::wstring(first, last) : std::wstring();
}

bool ReadFileBytes(const std::wstring& filePath, std::string* bytes, std::wstring* errorMessage) {
    const HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > MAXDWORD) {
        SetError(errorMessage, L"Unable to read blacklist");
        CloseHandle(file);
        return false;
    }

    bytes->assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    const bool read = bytes->empty() || ReadFile(file, bytes->data(), static_cast<DWORD>(bytes->size()), &bytesRead, nullptr);
    if (!read || bytesRead != bytes->size()) {
        SetError(errorMessage, L"Unable to read blacklist");
        CloseHandle(file);
        return false;
    }

    CloseHandle(file);
    return true;
}
}

bool ApplicationBlacklist::Load(const std::wstring& filePath, std::wstring* errorMessage) {
    std::string bytes;
    if (!ReadFileBytes(filePath, &bytes, errorMessage)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            ApplicationBlacklist empty;
            *this = std::move(empty);
            return true;
        }
        return false;
    }

    const std::wstring contents = EncodingUtils::Utf8ToWide(bytes, true, false);
    if (!bytes.empty() && contents.empty() && bytes != "\xEF\xBB\xBF") {
        if (errorMessage) {
            *errorMessage = L"Blacklist is not valid UTF-8";
        }
        return false;
    }

    ApplicationBlacklist loaded;
    size_t lineStart = 0;
    while (lineStart <= contents.size()) {
        const size_t lineEnd = contents.find(L'\n', lineStart);
        std::wstring line = contents.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        loaded.Add(line);
        if (lineEnd == std::wstring::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    *this = std::move(loaded);
    return true;
}

bool ApplicationBlacklist::Save(const std::wstring& filePath, std::wstring* errorMessage) const {
    const std::wstring tempPath = filePath + L".tmp";
    std::string bytes("\xEF\xBB\xBF");
    for (const std::wstring& path : m_paths) {
        bytes += EncodingUtils::WideToUtf8(path);
        bytes += '\n';
    }

    const HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        SetError(errorMessage, L"Unable to write blacklist");
        return false;
    }

    DWORD bytesWritten = 0;
    const bool written = (bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, nullptr)) &&
                         bytesWritten == bytes.size() && FlushFileBuffers(file);
    if (!CloseHandle(file) || !written) {
        SetError(errorMessage, L"Unable to write blacklist");
        DeleteFileW(tempPath.c_str());
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        SetError(errorMessage, L"Unable to replace blacklist");
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

bool ApplicationBlacklist::Add(const std::wstring& executablePath) {
    const std::wstring normalized = NormalizeExecutablePath(executablePath);
    if (normalized.empty() || Contains(normalized)) {
        return false;
    }
    m_paths.push_back(normalized);
    ++m_generation;
    return true;
}

bool ApplicationBlacklist::Remove(const std::wstring& executablePath) {
    const std::wstring normalized = NormalizeExecutablePath(executablePath);
    if (normalized.empty()) {
        return false;
    }
    const auto found = std::find_if(m_paths.begin(), m_paths.end(), [&normalized](const std::wstring& path) {
        return PathsEqual(path, normalized);
    });
    if (found == m_paths.end()) {
        return false;
    }
    m_paths.erase(found);
    ++m_generation;
    return true;
}

bool ApplicationBlacklist::Contains(const std::wstring& executablePath) const {
    return std::any_of(m_paths.begin(), m_paths.end(), [&executablePath](const std::wstring& path) {
        return PathsEqual(path, executablePath);
    });
}

const std::vector<std::wstring>& ApplicationBlacklist::Paths() const noexcept {
    return m_paths;
}

std::uint64_t ApplicationBlacklist::Generation() const noexcept {
    return m_generation;
}

std::wstring ApplicationBlacklist::NormalizeExecutablePath(const std::wstring& path) {
    const std::wstring trimmed = Trim(path);
    const std::filesystem::path executablePath(trimmed);
    if (!executablePath.is_absolute() ||
        CompareStringOrdinal(executablePath.extension().c_str(), -1, L".exe", -1, TRUE) != CSTR_EQUAL) {
        return std::wstring();
    }
    return executablePath.lexically_normal().wstring();
}

bool ApplicationBlacklist::PathsEqual(const std::wstring& left, const std::wstring& right) noexcept {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
