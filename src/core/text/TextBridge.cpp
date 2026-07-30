#include "TextBridge.h"

#include "ClipboardUtils.h"

#include <cwchar>
namespace {
bool IsModifierPressed(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
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

bool TextBridge::TypeText(const std::wstring& text, size_t* typedChars) const {
    if (typedChars) {
        *typedChars = 0;
    }
    WaitForModifiersRelease();
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        bool sent = false;
        if (ch == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;
            }
            sent = SendKey(VK_RETURN);
        } else if (ch == L'\n') {
            sent = SendKey(VK_RETURN);
        } else if (ch == L'\t') {
            sent = SendKey(VK_TAB);
        } else {
            sent = SendUnicodeChar(ch);
        }
        if (!sent) {
            return false;
        }
        if (typedChars) {
            ++(*typedChars);
        }
    }
    return true;
}

bool TextBridge::DeleteCharacters(size_t count) const {
    if (count == 0) {
        return true;
    }
    WaitForModifiersRelease();
    return SendRepeatedKey(VK_BACK, count);
}

bool TextBridge::SelectPreviousCharacters(size_t count) const {
    if (count == 0) {
        return true;
    }
    WaitForModifiersRelease();
    return SendRepeatedShiftKey(VK_LEFT, count);
}

bool TextBridge::CollapseSelection() const {
    WaitForModifiersRelease();

    return SendKey(VK_RIGHT);
}

std::wstring TextBridge::CopyFromActiveControl(bool selectAll) const {
    ClipboardUtils::Snapshot snapshot;

    WaitForModifiersRelease();

    if (selectAll) {
        SendCtrlShortcut('A');
        Sleep(20);
    }

    const int waitAttempts = selectAll ? 80 : 12;
    const int waitSleepMs = selectAll ? 10 : 5;
    std::wstring copied;
    if (TryCopyShortcut(waitAttempts, waitSleepMs, &copied)) {
        return copied;
    }
    return L"";
}

bool TextBridge::PasteIntoActiveControl(const std::wstring& text, bool selectAll) const {
    ClipboardUtils::Snapshot snapshot;
    WaitForModifiersRelease();
    if (!ClipboardUtils::WriteText(nullptr, text)) {
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

bool TextBridge::TryCopyShortcut(int waitAttempts, int waitSleepMs, std::wstring* copied) const {
    if (copied) {
        copied->clear();
    }

    const DWORD sequenceBefore = GetClipboardSequenceNumber();
    std::wstring clipboardBefore;
    const bool hadClipboardText = ClipboardUtils::ReadText(nullptr, &clipboardBefore);
    if (!SendCtrlShortcut('C')) {
        return false;
    }

    const bool clipboardChanged = WaitForClipboardChange(sequenceBefore, waitAttempts, waitSleepMs);

    std::wstring currentClipboardText;
    if (!ClipboardUtils::ReadText(nullptr, &currentClipboardText)) {
        return false;
    }
    if (currentClipboardText.empty()) {
        return false;
    }
    if (!clipboardChanged && (!hadClipboardText || currentClipboardText == clipboardBefore)) {
        return false;
    }

    if (copied) {
        *copied = currentClipboardText;
    }
    return true;
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

bool TextBridge::SendKey(WORD virtualKey) const {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = virtualKey;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = virtualKey;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

bool TextBridge::SendUnicodeChar(wchar_t ch) const {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = ch;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
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

bool TextBridge::SendRepeatedShiftKey(WORD virtualKey, size_t count) const {
    for (size_t i = 0; i < count; ++i) {
        INPUT inputs[4] = {};

        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_SHIFT;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = virtualKey;

        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = virtualKey;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = VK_SHIFT;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

        if (SendInput(4, inputs, sizeof(INPUT)) != 4) {
            return false;
        }
    }
    return true;
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
