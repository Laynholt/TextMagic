#include "ScriptManifest.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / L"TextMagic-manifest-tests";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);

    {
        std::ofstream manifest(directory / L"UPPERCASE.TMSCRIPT", std::ios::binary);
        manifest << "name=Uppercase\n"
                    "hotkey=Ctrl+Alt+U\n"
                    "command=cmd.exe\n";
    }

    const ScriptManifest::LoadResult result =
        ScriptManifest::LoadFromDirectory(directory.wstring());
    const bool passed = result.entries.size() == 1
        && result.entries.front().name == L"Uppercase";
    if (!passed) {
        std::cerr << "FAIL: uppercase .TMSCRIPT extension was not loaded\n";
    }

    std::filesystem::remove_all(directory, error);
    return passed ? 0 : 1;
}
