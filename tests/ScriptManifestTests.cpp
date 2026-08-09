#include "ScriptManifest.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
const ScriptManifest::Entry* FindByName(
    const std::vector<ScriptManifest::Entry>& entries,
    const wchar_t* name
) {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}
}

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
    {
        std::ofstream manifest(directory / L"auto.tmscript", std::ios::binary);
        manifest << "name=Auto\n"
                    "hotkey=Ctrl+Alt+A\n"
                    "command=cmd.exe\n"
                    "output_layout=auto\n";
    }
    {
        std::ofstream manifest(directory / L"default.tmscript", std::ios::binary);
        manifest << "name=Default\n"
                    "hotkey=Ctrl+Alt+D\n"
                    "command=cmd.exe\n";
    }
    {
        std::ofstream manifest(directory / L"invalid.tmscript", std::ios::binary);
        manifest << "name=Invalid\n"
                    "hotkey=Ctrl+Alt+I\n"
                    "command=cmd.exe\n"
                    "output_layout=unsupported\n";
    }

    const ScriptManifest::LoadResult result =
        ScriptManifest::LoadFromDirectory(directory.wstring());
    bool passed = Check(result.entries.size() == 4, "all temporary manifests were loaded");
    passed &= Check(FindByName(result.entries, L"Uppercase"),
        "uppercase .TMSCRIPT extension was loaded");

    const auto autoEntry = FindByName(result.entries, L"Auto");
    const auto defaultEntry = FindByName(result.entries, L"Default");
    const auto invalidEntry = FindByName(result.entries, L"Invalid");
    passed &= Check(autoEntry && autoEntry->autoOutputLayout,
        "output_layout=auto enables automatic output layout");
    passed &= Check(defaultEntry && !defaultEntry->autoOutputLayout,
        "missing output_layout keeps automatic output layout disabled");
    passed &= Check(invalidEntry && !invalidEntry->autoOutputLayout,
        "unsupported output_layout stays disabled");

    std::filesystem::remove_all(directory, error);
    return passed ? 0 : 1;
}
