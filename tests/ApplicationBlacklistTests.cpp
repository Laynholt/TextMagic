#include "ApplicationBlacklist.h"
#include "EncodingUtils.h"

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

std::vector<char> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void WriteBytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
}

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / L"TextMagic-ApplicationBlacklistTests";
    const std::filesystem::path path = directory / L"applications.txt";
    const std::filesystem::path missingPath = directory / L"missing.txt";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory, ignored);

    std::wstring error;
    ApplicationBlacklist blacklist;
    Expect(blacklist.Load(missingPath.wstring(), &error), "missing file must load successfully");
    Expect(blacklist.Paths().empty(), "missing file must load an empty blacklist");

    const std::wstring cyrillicPath = L"C:\\Apps\\\u041f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u0430.EXE";
    Expect(blacklist.Add(cyrillicPath), "absolute executable path must be added");
    Expect(blacklist.Save(path.wstring(), &error), "UTF-8 blacklist save must succeed");

    ApplicationBlacklist loaded;
    Expect(loaded.Load(path.wstring(), &error), "saved blacklist must load successfully");
    Expect(loaded.Contains(L"c:\\apps\\\u041f\u0420\u041e\u0413\u0420\u0410\u041c\u041c\u0410.exe"),
           "UTF-8 Cyrillic executable path must round-trip case-insensitively");
    Expect(loaded.Paths().size() == 1, "round-trip must preserve one path");

    const std::filesystem::path invalidPath = directory / L"invalid.txt";
    const std::string invalidLines =
        "\xEF\xBB\xBF\r\nrelative.exe\nC:\\Apps\\tool.dll\nC:\\Apps\\Tool.exe\nC:\\apps\\TOOL.EXE\n";
    WriteBytes(invalidPath, std::vector<char>(invalidLines.begin(), invalidLines.end()));
    ApplicationBlacklist filtered;
    Expect(filtered.Load(invalidPath.wstring(), &error), "invalid-lines blacklist must load successfully");
    Expect(filtered.Paths().size() == 1, "blank, relative, non-exe, and duplicate paths must be ignored");
    Expect(filtered.Contains(L"C:\\APPS\\tool.exe"), "Contains must use ordinal case-insensitive comparison");

    const std::uint64_t initialGeneration = filtered.Generation();
    Expect(!filtered.Add(L"C:\\apps\\TOOL.exe"), "case variant duplicate must not be added");
    Expect(filtered.Generation() == initialGeneration, "duplicate add must not change generation");
    Expect(filtered.Add(L"C:\\Apps\\Other.exe"), "new executable must be added");
    Expect(filtered.Generation() == initialGeneration + 1, "new add must change generation");
    Expect(!filtered.Remove(L"C:\\Apps\\Missing.exe"), "missing executable must not be removed");
    Expect(filtered.Generation() == initialGeneration + 1, "missing remove must not change generation");
    Expect(filtered.Remove(L"C:\\APPS\\other.EXE"), "case-insensitive executable must be removed");
    Expect(filtered.Generation() == initialGeneration + 2, "successful remove must change generation");

    Expect(filtered.Save(path.wstring(), &error), "baseline blacklist save must succeed");
    const std::vector<char> oldBytes = ReadBytes(path);
    Expect(filtered.Add(L"C:\\Apps\\Locked.exe"), "locked-save path must be added");
    const HANDLE lock = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(lock != INVALID_HANDLE_VALUE, "destination must be locked for replacement");
    Expect(!filtered.Save(path.wstring(), &error), "save must fail when destination is locked");
    CloseHandle(lock);
    Expect(ReadBytes(path) == oldBytes, "failed save must preserve old destination bytes");

    std::filesystem::remove_all(directory, ignored);
    return 0;
}
