#pragma once

#include <windows.h>

#include <string>

class TextBridge {
public:
    std::wstring GetSelectedText() const;
    bool SetSelectedText(const std::wstring& text) const;
    bool WaitForModifiersRelease(HWND expectedTarget = nullptr) const;
    bool TypeText(HWND expectedTarget,
                  const std::wstring& text,
                  size_t* typedChars = nullptr) const;
    bool DeleteCharacters(HWND expectedTarget, const std::wstring& text) const;

private:
    std::wstring CopyFromActiveControl() const;
    bool PasteIntoActiveControl(const std::wstring& text) const;
    bool TryCopyShortcut(int waitAttempts, int waitSleepMs, std::wstring* copied) const;
    bool SendCtrlShortcut(WORD virtualKey) const;
    bool SendKey(WORD virtualKey) const;
    bool SendKeyDown(WORD virtualKey) const;
    bool SendKeyUp(WORD virtualKey) const;
    UINT SendUnicodeChar(wchar_t ch) const;
    bool SendRepeatedKey(HWND expectedTarget, WORD virtualKey, size_t count) const;
    bool SelectPreviousCharacters(HWND expectedTarget, const std::wstring& text) const;

    static bool WaitForClipboardChange(DWORD initialSequence, int maxAttempts = 80, int sleepMs = 10);
};
