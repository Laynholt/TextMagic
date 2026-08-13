#pragma once

#include <windows.h>

#include <string>

class TextBridge {
public:
    std::wstring GetSelectedText(HWND expectedTarget) const;
    bool SetSelectedText(HWND expectedTarget, const std::wstring& text) const;
    bool WaitForModifiersRelease(HWND expectedTarget = nullptr, int maxAttempts = 60) const;
    bool ReplaceText(HWND expectedTarget,
                     const std::wstring& oldText,
                     const std::wstring& replacement) const;

private:
    std::wstring CopyFromActiveControl(HWND expectedTarget) const;
    bool PasteIntoActiveControl(HWND expectedTarget, const std::wstring& text) const;
    bool TryCopyShortcut(HWND expectedTarget,
                         int waitAttempts,
                         int waitSleepMs,
                         std::wstring* copied) const;
    bool SendCtrlShortcut(HWND expectedTarget, WORD virtualKey) const;

    static bool WaitForClipboardChange(DWORD initialSequence, int maxAttempts = 80, int sleepMs = 10);
};
