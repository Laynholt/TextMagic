#pragma once

#include <windows.h>

#include <string>

class TextBridge {
public:
    std::wstring GetSelectedText() const;
    bool SetSelectedText(const std::wstring& text) const;
    bool WaitForModifiersRelease(HWND expectedTarget = nullptr) const;
    bool ReplaceText(HWND expectedTarget,
                     const std::wstring& oldText,
                     const std::wstring& replacement) const;

private:
    std::wstring CopyFromActiveControl() const;
    bool PasteIntoActiveControl(const std::wstring& text) const;
    bool TryCopyShortcut(int waitAttempts, int waitSleepMs, std::wstring* copied) const;
    bool SendCtrlShortcut(WORD virtualKey) const;

    static bool WaitForClipboardChange(DWORD initialSequence, int maxAttempts = 80, int sleepMs = 10);
};
