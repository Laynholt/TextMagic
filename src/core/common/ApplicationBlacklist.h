#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ApplicationBlacklist {
public:
    bool Load(const std::wstring& filePath, std::wstring* errorMessage);
    bool Save(const std::wstring& filePath, std::wstring* errorMessage) const;
    bool Add(const std::wstring& executablePath);
    bool Remove(const std::wstring& executablePath);
    bool Contains(const std::wstring& executablePath) const;
    const std::vector<std::wstring>& Paths() const noexcept;
    std::uint64_t Generation() const noexcept;

    static std::wstring NormalizeExecutablePath(const std::wstring& path);
    static bool PathsEqual(const std::wstring& left, const std::wstring& right) noexcept;

private:
    std::vector<std::wstring> m_paths;
    std::uint64_t m_generation = 1;
};
