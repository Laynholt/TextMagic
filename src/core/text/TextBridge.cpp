#include "TextBridge.h"

#include <cstring>
#include <cwchar>

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

bool TextBridge::CollapseSelection() const {
    WaitForModifiersRelease();

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_RIGHT;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_RIGHT;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

std::wstring TextBridge::CopyFromActiveControl(bool selectAll) const {
    ClipboardSnapshot snapshot;

    WaitForModifiersRelease();
    const DWORD sequenceBefore = GetClipboardSequenceNumber();

    if (selectAll) {
        SendCtrlShortcut('A');
        Sleep(20);
    }

    if (!SendCtrlShortcut('C')) {
        return L"";
    }

    if (!WaitForClipboardChange(sequenceBefore)) {
        return L"";
    }

    return GetClipboardUnicodeText();
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

bool TextBridge::WaitForClipboardChange(DWORD initialSequence) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (GetClipboardSequenceNumber() != initialSequence) {
            return true;
        }
        Sleep(10);
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
    if (!handle) {
        CloseClipboard();
        return L"";
    }

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
