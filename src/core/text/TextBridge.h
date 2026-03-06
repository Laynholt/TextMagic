#pragma once

#include <windows.h>

#include <objidl.h>
#include <string>

class TextBridge {
public:
    std::wstring GetAllText() const;
    bool SetAllText(const std::wstring& text) const;

    std::wstring GetSelectedText() const;
    bool SetSelectedText(const std::wstring& text) const;
    bool DeleteCharacters(size_t count) const;
    bool CollapseSelection() const;

private:
    class ClipboardSnapshot {
    public:
        ClipboardSnapshot();
        ~ClipboardSnapshot();

        ClipboardSnapshot(const ClipboardSnapshot&) = delete;
        ClipboardSnapshot& operator=(const ClipboardSnapshot&) = delete;

    private:
        void Restore();

        IDataObject* m_dataObject = nullptr;
        bool m_restored = false;
    };

    std::wstring CopyFromActiveControl(bool selectAll) const;
    bool PasteIntoActiveControl(const std::wstring& text, bool selectAll) const;
    bool SendCtrlShortcut(WORD virtualKey) const;
    bool SendCtrlShiftShortcut(WORD virtualKey) const;
    bool SendKey(WORD virtualKey) const;
    bool SendRepeatedKey(WORD virtualKey, size_t count) const;
    bool IsTerminalFocusedControl() const;

    static bool WaitForClipboardChange(DWORD initialSequence, int maxAttempts = 80, int sleepMs = 10);
    static bool SetClipboardUnicodeText(const std::wstring& text);
    static std::wstring GetClipboardUnicodeText();
};
