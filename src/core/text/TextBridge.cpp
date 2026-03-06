#include "TextBridge.h"

#include <cstring>
#include <cwchar>
#include <cwctype>

namespace {
bool OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (OpenClipboard(owner)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

bool IsModifierPressed(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool IsTerminalClassName(const wchar_t* className) {
    if (!className || className[0] == L'\0') {
        return false;
    }
    return _wcsicmp(className, L"ConsoleWindowClass") == 0
        || _wcsicmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0;
}

bool IsWordSeparator(wchar_t ch) {
    return iswspace(ch);
}

void WaitForModifiersRelease() {
    for (int attempt = 0; attempt < 60; ++attempt) {
        const bool controlDown = IsModifierPressed(VK_CONTROL);
        const bool altDown = IsModifierPressed(VK_MENU);
        const bool shiftDown = IsModifierPressed(VK_SHIFT);
        const bool lwinDown = IsModifierPressed(VK_LWIN);
        const bool rwinDown = IsModifierPressed(VK_RWIN);
        if (!controlDown && !altDown && !shiftDown && !lwinDown && !rwinDown) {
            return;
        }
        Sleep(5);
    }
}
}

TextBridge::ClipboardSnapshot::ClipboardSnapshot() {
    IDataObject* clipboardObject = nullptr;
    if (SUCCEEDED(OleGetClipboard(&clipboardObject)) && clipboardObject) {
        m_dataObject = clipboardObject;
    }
}

TextBridge::ClipboardSnapshot::~ClipboardSnapshot() {
    Restore();
    if (m_dataObject) {
        m_dataObject->Release();
        m_dataObject = nullptr;
    }
}

void TextBridge::ClipboardSnapshot::Restore() {
    if (m_restored || !m_dataObject) {
        return;
    }

    OleSetClipboard(m_dataObject);
    m_restored = true;
}

std::wstring TextBridge::GetAllText() const {
    return CopyFromActiveControl(true);
}

bool TextBridge::SetAllText(const std::wstring& text) const {
    return PasteIntoActiveControl(text, true);
}

std::wstring TextBridge::GetSelectedText() const {
    return CopyFromActiveControl(false);
}

bool TextBridge::SetSelectedText(const std::wstring& text) const {
    return PasteIntoActiveControl(text, false);
}

bool TextBridge::DeleteCharacters(size_t count) const {
    if (count == 0) {
        return true;
    }
    WaitForModifiersRelease();
    return SendRepeatedKey(VK_BACK, count);
}

bool TextBridge::CollapseSelection() const {
    WaitForModifiersRelease();

    return SendKey(VK_RIGHT);
}

std::wstring TextBridge::CopyFromActiveControl(bool selectAll) const {
    ClipboardSnapshot snapshot;

    WaitForModifiersRelease();
    const DWORD sequenceBefore = GetClipboardSequenceNumber();
    const std::wstring clipboardBefore = GetClipboardUnicodeText();

    if (selectAll) {
        SendCtrlShortcut('A');
        Sleep(20);
    }

    if (!SendCtrlShortcut('C')) {
        return L"";
    }

    const int waitAttempts = selectAll ? 80 : 12;
    const int waitSleepMs = selectAll ? 10 : 5;
    const bool changed = WaitForClipboardChange(sequenceBefore, waitAttempts, waitSleepMs);
    const std::wstring copied = GetClipboardUnicodeText();
    if (copied.empty()) {
        return L"";
    }
    if (!changed && GetClipboardSequenceNumber() == sequenceBefore && copied == clipboardBefore) {
        return L"";
    }
    return copied;
}

bool TextBridge::PasteIntoActiveControl(const std::wstring& text, bool selectAll) const {
    ClipboardSnapshot snapshot;
    WaitForModifiersRelease();
    if (!SetClipboardUnicodeText(text)) {
        return false;
    }

    if (selectAll && IsTerminalFocusedControl()) {
        return false;
    }

    if (selectAll) {
        SendCtrlShortcut('A');
        Sleep(20);
    }

    const bool pasted = SendCtrlShortcut('V');
    Sleep(20);
    return pasted;
}

bool TextBridge::SendCtrlShortcut(WORD virtualKey) const {
    INPUT inputs[4] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = virtualKey;

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = virtualKey;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

bool TextBridge::SendCtrlShiftShortcut(WORD virtualKey) const {
    INPUT inputs[6] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_SHIFT;

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = virtualKey;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = virtualKey;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[4].type = INPUT_KEYBOARD;
    inputs[4].ki.wVk = VK_SHIFT;
    inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[5].type = INPUT_KEYBOARD;
    inputs[5].ki.wVk = VK_CONTROL;
    inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(6, inputs, sizeof(INPUT)) == 6;
}

bool TextBridge::SendKey(WORD virtualKey) const {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = virtualKey;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = virtualKey;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

bool TextBridge::SendRepeatedKey(WORD virtualKey, size_t count) const {
    for (size_t i = 0; i < count; ++i) {
        if (!SendKey(virtualKey)) {
            return false;
        }
    }
    return true;
}

bool TextBridge::IsTerminalFocusedControl() const {
    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }

    DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    if (threadId == 0) {
        return false;
    }

    GUITHREADINFO guiInfo = {};
    guiInfo.cbSize = sizeof(guiInfo);
    if (!GetGUIThreadInfo(threadId, &guiInfo)) {
        return false;
    }

    HWND focusedWindow = guiInfo.hwndFocus ? guiInfo.hwndFocus : guiInfo.hwndActive;
    if (!focusedWindow) {
        focusedWindow = foreground;
    }

    wchar_t className[128] = {};
    if (GetClassNameW(focusedWindow, className, static_cast<int>(_countof(className))) > 0) {
        if (IsTerminalClassName(className)) {
            return true;
        }
    }

    HWND rootWindow = GetAncestor(focusedWindow, GA_ROOT);
    if (rootWindow && rootWindow != focusedWindow) {
        ZeroMemory(className, sizeof(className));
        if (GetClassNameW(rootWindow, className, static_cast<int>(_countof(className))) > 0) {
            if (IsTerminalClassName(className)) {
                return true;
            }
        }
    }

    return false;
}

bool TextBridge::WaitForClipboardChange(DWORD initialSequence, int maxAttempts, int sleepMs) {
    if (maxAttempts <= 0) {
        return false;
    }
    if (sleepMs < 0) {
        sleepMs = 0;
    }
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (GetClipboardSequenceNumber() != initialSequence) {
            return true;
        }
        Sleep(static_cast<DWORD>(sleepMs));
    }
    return false;
}

bool TextBridge::SetClipboardUnicodeText(const std::wstring& text) {
    if (!OpenClipboardWithRetry(nullptr)) {
        return false;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }

    void* locked = GlobalLock(memory);
    if (!locked) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

std::wstring TextBridge::GetClipboardUnicodeText() {
    if (!OpenClipboardWithRetry(nullptr)) {
        return L"";
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const wchar_t* raw = static_cast<const wchar_t*>(GlobalLock(handle));
        if (!raw) {
            CloseClipboard();
            return L"";
        }

        std::wstring text(raw);
        GlobalUnlock(handle);
        CloseClipboard();
        return text;
    }

    handle = GetClipboardData(CF_TEXT);
    if (!handle) {
        CloseClipboard();
        return L"";
    }

    const char* rawAnsi = static_cast<const char*>(GlobalLock(handle));
    if (!rawAnsi) {
        CloseClipboard();
        return L"";
    }

    const int requiredChars = MultiByteToWideChar(CP_ACP, 0, rawAnsi, -1, nullptr, 0);
    if (requiredChars <= 0) {
        GlobalUnlock(handle);
        CloseClipboard();
        return L"";
    }

    std::wstring wideText(static_cast<size_t>(requiredChars), L'\0');
    MultiByteToWideChar(CP_ACP, 0, rawAnsi, -1, &wideText[0], requiredChars);
    GlobalUnlock(handle);
    CloseClipboard();
    if (!wideText.empty() && wideText.back() == L'\0') {
        wideText.pop_back();
    }
    return wideText;
}
