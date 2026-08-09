#include "LogFile.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}
}

int main() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / L"TextMagic-LogFileTests.log";
    const std::filesystem::path backupPath = path.wstring() + L".old";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(backupPath, ignored);

    Expect(LogFile::Append(path.wstring(), L"first"), "first log append must succeed");
    const std::wstring second = L"\u0432\u0442\u043e\u0440\u0430\u044f";
    Expect(LogFile::Append(path.wstring(), second), "UTF-8 log append must succeed");
    Expect(LogFile::Read(path.wstring()) == L"first\r\n" + second,
           "the UI must read the complete persisted UTF-8 log");
    Expect(LogFile::Clear(path.wstring()), "log clear must succeed");
    Expect(LogFile::Read(path.wstring()).empty(), "cleared log must read as empty");
    Expect(LogFile::Append(path.wstring(), L"new session"),
           "new-session log append must succeed");
    Expect(LogFile::Read(path.wstring()) == L"new session",
           "new session must not retain previous log lines");

    Expect(LogFile::Clear(path.wstring()), "rotation fixture must be cleared");
    Expect(LogFile::Append(path.wstring(), L"12345", 10),
           "pre-rotation append must succeed");
    Expect(LogFile::Append(path.wstring(), L"6789", 10),
           "rotation append must succeed");
    Expect(LogFile::Read(path.wstring()) == L"6789",
           "current log must contain only post-rotation entries");
    Expect(LogFile::Read(backupPath.wstring()) == L"12345",
           "one bounded backup must retain the rotated log");
    Expect(LogFile::Clear(path.wstring()), "clearing a rotated log must succeed");
    Expect(!std::filesystem::exists(backupPath),
           "clearing logs must also remove the rotated backup");

    std::filesystem::remove(path, ignored);
    std::filesystem::remove(backupPath, ignored);
    return 0;
}
