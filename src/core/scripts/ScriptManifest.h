#pragma once

#include <windows.h>

#include <string>
#include <vector>

class ScriptManifest {
public:
    enum class Action {
        TransformText,
        CycleKeyboardLayout,
    };

    enum class HotkeyKind {
        KeyChord,
        ModifierGesture,
        ModifierDoubleTap,
    };

    struct Entry {
        std::wstring name;
        std::wstring description;
        std::wstring hotkeyText;
        std::wstring commandLine;
        std::wstring scriptBody;
        std::wstring manifestPath;
        UINT modifiers = 0;
        UINT virtualKey = 0;
        Action action = Action::TransformText;
        HotkeyKind hotkeyKind = HotkeyKind::KeyChord;
        bool enabled = true;
        bool autoOutputLayout = false;
    };

    struct LoadResult {
        std::vector<Entry> entries;
        std::wstring warning;
    };

    static LoadResult LoadFromDirectory(const std::wstring& directoryPath);
    static bool SetEnabledInFile(const std::wstring& manifestPath, bool enabled, std::wstring* error);
    static bool ParseHotkey(
        const std::wstring& hotkeyText,
        HotkeyKind* kind,
        UINT* modifiers,
        UINT* virtualKey,
        std::wstring* error);
};
