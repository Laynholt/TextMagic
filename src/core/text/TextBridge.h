#pragma once

#include <windows.h>

#include <string>

class TextBridge {
public:
    std::wstring GetSelectedText() const;
    bool SetSelectedText(const std::wstring& text) const;
    bool WaitForModifiersRelease() const;
    bool TypeText(const std::wstring& text, size_t* typedChars = nullptr) const;
    bool DeleteCharacters(size_t count) const;

private:
    std::wstring CopyFromActiveControl() const;
    bool PasteIntoActiveControl(const std::wstring& text) const;
    bool TryCopyShortcut(int waitAttempts, int waitSleepMs, std::wstring* copied) const;
    bool SendCtrlShortcut(WORD virtualKey) const;
    bool SendKey(WORD virtualKey) const;
    bool SendKeyUp(WORD virtualKey) const;
    UINT SendUnicodeChar(wchar_t ch) const;
    bool SendRepeatedKey(WORD virtualKey, size_t count) const;
    bool SelectPreviousCharacters(size_t count) const;

    static bool WaitForClipboardChange(DWORD initialSequence, int maxAttempts = 80, int sleepMs = 10);
};
