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

bool CheckHotkey(
    const wchar_t* text,
    ScriptManifest::HotkeyKind expectedKind,
    UINT expectedModifiers,
    UINT expectedVirtualKey
) {
    ScriptManifest::HotkeyKind kind = ScriptManifest::HotkeyKind::KeyChord;
    UINT modifiers = 0;
    UINT virtualKey = 0;
    std::wstring error;
    const bool parsed = ScriptManifest::ParseHotkey(
        text, &kind, &modifiers, &virtualKey, &error);
    bool passed = Check(parsed, "hotkey form parses");
    passed &= Check(kind == expectedKind, "hotkey kind is normalized");
    passed &= Check(modifiers == expectedModifiers, "modifier mask is normalized");
    passed &= Check(virtualKey == expectedVirtualKey, "primary key is normalized");
    return passed;
}

bool CheckHotkeyRejects(const wchar_t* text) {
    ScriptManifest::HotkeyKind kind = ScriptManifest::HotkeyKind::KeyChord;
    UINT modifiers = 0;
    UINT virtualKey = 0;
    std::wstring error;
    return Check(!ScriptManifest::ParseHotkey(
                     text, &kind, &modifiers, &virtualKey, &error),
                 "invalid hotkey form is rejected");
}
}

int main() {
    bool passed = true;
    passed &= CheckHotkey(L"Shift", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_SHIFT, VK_SHIFT);
    passed &= CheckHotkey(L"Ctrl", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_CONTROL, VK_CONTROL);
    passed &= CheckHotkey(L"CONTROL", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_CONTROL, VK_CONTROL);
    passed &= CheckHotkey(L"Alt", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_ALT, VK_MENU);
    passed &= CheckHotkey(L"Win", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_WIN, VK_LWIN);
    passed &= CheckHotkey(L"Windows", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_WIN, VK_LWIN);
    passed &= CheckHotkey(L"Ctrl+Shift", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_CONTROL | MOD_SHIFT, 0);
    passed &= CheckHotkey(L"Shift+Ctrl", ScriptManifest::HotkeyKind::ModifierGesture,
        MOD_CONTROL | MOD_SHIFT, 0);
    passed &= CheckHotkey(L"Shift+Shift", ScriptManifest::HotkeyKind::ModifierDoubleTap,
        MOD_SHIFT, VK_SHIFT);
    passed &= CheckHotkey(L"Ctrl+Alt+L", ScriptManifest::HotkeyKind::KeyChord,
        MOD_CONTROL | MOD_ALT, 'L');
    passed &= CheckHotkeyRejects(L"L");
    passed &= CheckHotkeyRejects(L"Ctrl+L+M");
    passed &= CheckHotkeyRejects(L"Shift+Shift+Shift");
    passed &= CheckHotkeyRejects(L"Ctrl+Ctrl+L");
    passed &= CheckHotkeyRejects(L"Ctrl+Shift+Ctrl");

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
    {
        std::ofstream manifest(directory / L"cycle.tmscript", std::ios::binary);
        manifest << "name=Cycle\n"
                    "hotkey=Shift\n"
                    "action=cycle_keyboard_layout\n"
                    "enabled=true\n";
    }
    {
        std::ofstream manifest(directory / L"unknown-action.tmscript", std::ios::binary);
        manifest << "name=Unknown action\n"
                    "hotkey=Shift\n"
                    "action=unknown\n"
                    "command=cmd.exe\n";
    }

    const ScriptManifest::LoadResult result =
        ScriptManifest::LoadFromDirectory(directory.wstring());
    passed &= Check(result.entries.size() == 5, "all supported temporary manifests were loaded");
    passed &= Check(FindByName(result.entries, L"Uppercase"),
        "uppercase .TMSCRIPT extension was loaded");

    const auto autoEntry = FindByName(result.entries, L"Auto");
    const auto defaultEntry = FindByName(result.entries, L"Default");
    const auto invalidEntry = FindByName(result.entries, L"Invalid");
    const auto cycleEntry = FindByName(result.entries, L"Cycle");
    const auto unknownEntry = FindByName(result.entries, L"Unknown action");
    passed &= Check(autoEntry && autoEntry->autoOutputLayout,
        "output_layout=auto enables automatic output layout");
    passed &= Check(defaultEntry && !defaultEntry->autoOutputLayout,
        "missing output_layout keeps automatic output layout disabled");
    passed &= Check(invalidEntry && !invalidEntry->autoOutputLayout,
        "unsupported output_layout stays disabled");
    passed &= Check(defaultEntry && defaultEntry->action == ScriptManifest::Action::TransformText,
        "missing action defaults to text transformation");
    passed &= Check(defaultEntry && defaultEntry->hotkeyKind == ScriptManifest::HotkeyKind::KeyChord,
        "primary hotkeys keep key-chord kind");
    passed &= Check(cycleEntry && cycleEntry->action == ScriptManifest::Action::CycleKeyboardLayout,
        "cycle action loads without executable fields");
    passed &= Check(cycleEntry && cycleEntry->hotkeyKind == ScriptManifest::HotkeyKind::ModifierGesture,
        "cycle modifier gesture kind is preserved");
    passed &= Check(!unknownEntry, "unknown action manifest is skipped");
    passed &= Check(result.warning.find(L"unknown-action.tmscript") != std::wstring::npos,
        "unknown action warning names the manifest");
    passed &= Check(result.warning.find(L": unknown\n") != std::wstring::npos,
        "unknown action warning names the unsupported action");

    std::filesystem::remove_all(directory, error);
    return passed ? 0 : 1;
}
