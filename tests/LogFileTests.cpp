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
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

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

    std::filesystem::remove(path, ignored);
    return 0;
}
