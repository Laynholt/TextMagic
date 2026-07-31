#include "TextBridge.h"

#include "ClipboardUtils.h"
#include "TextBridgeInputUtils.h"

#include <cwchar>
#include <limits>
#include <vector>

namespace {
constexpr size_t DIRECT_BACKSPACE_LIMIT = 500;

constexpr bool ShouldSelectBeforeDelete(size_t count) {
    return count > DIRECT_BACKSPACE_LIMIT;
}

static_assert(!ShouldSelectBeforeDelete(500));
static_assert(ShouldSelectBeforeDelete(501));

bool IsModifierPressed(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

}

bool TextBridge::WaitForModifiersRelease() const {
    for (int attempt = 0; attempt < 60; ++attempt) {
        const bool controlDown = IsModifierPressed(VK_CONTROL);
        const bool altDown = IsModifierPressed(VK_MENU);
        const bool shiftDown = IsModifierPressed(VK_SHIFT);
        const bool lwinDown = IsModifierPressed(VK_LWIN);
        const bool rwinDown = IsModifierPressed(VK_RWIN);
        if (!controlDown && !altDown && !shiftDown && !lwinDown && !rwinDown) {
            return true;
        }
        Sleep(5);
    }
    return false;
}

std::wstring TextBridge::GetSelectedText() const {
    return CopyFromActiveControl();
}

bool TextBridge::SetSelectedText(const std::wstring& text) const {
    return PasteIntoActiveControl(text);
}

bool TextBridge::TypeText(const std::wstring& text, size_t* typedChars) const {
    if (typedChars) {
        *typedChars = 0;
    }
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        bool sent = false;
        bool unicodeCharMayHaveBeenTyped = false;
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
            const UINT sentInputs = SendUnicodeChar(ch);
            sent = sentInputs == 2;
            unicodeCharMayHaveBeenTyped = sentInputs > 0;
        }
        if (!sent) {
            if (typedChars && unicodeCharMayHaveBeenTyped) {
                ++(*typedChars);
            }
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
    if (!ShouldSelectBeforeDelete(count)) {
        return SendRepeatedKey(VK_BACK, count);
    }
    if (!SelectPreviousCharacters(count)) {
        return false;
    }
    if (SendKey(VK_BACK)) {
        return true;
    }
    SendKey(VK_RIGHT);
    return false;
}

std::wstring TextBridge::CopyFromActiveControl() const {
    ClipboardUtils::Snapshot snapshot;

    if (!WaitForModifiersRelease()) {
        return L"";
    }

    std::wstring copied;
    if (TryCopyShortcut(12, 5, &copied)) {
        return copied;
    }
    return L"";
}

bool TextBridge::PasteIntoActiveControl(const std::wstring& text) const {
    ClipboardUtils::Snapshot snapshot;
    if (!WaitForModifiersRelease()) {
        return false;
    }
    if (!ClipboardUtils::WriteText(nullptr, text)) {
        return false;
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
    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent == 1) {
        SendKeyUp(virtualKey);
        return true;
    }
    return sent == 2;
}

bool TextBridge::SendKeyUp(WORD virtualKey) const {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

UINT TextBridge::SendUnicodeChar(wchar_t ch) const {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = ch;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT));
}

bool TextBridge::SendRepeatedKey(WORD virtualKey, size_t count) const {
    for (size_t i = 0; i < count; ++i) {
        if (!SendKey(virtualKey)) {
            return false;
        }
    }
    return true;
}

bool TextBridge::SelectPreviousCharacters(size_t count) const {
    if (count > ((std::numeric_limits<UINT>::max)() - 2) / 2) {
        return false;
    }

    std::vector<INPUT> inputs;
    inputs.reserve(2 + count * 2);

    INPUT shiftDown = {};
    shiftDown.type = INPUT_KEYBOARD;
    shiftDown.ki.wVk = VK_SHIFT;
    inputs.push_back(shiftDown);

    for (size_t i = 0; i < count; ++i) {
        INPUT leftDown = {};
        leftDown.type = INPUT_KEYBOARD;
        leftDown.ki.wVk = VK_LEFT;
        inputs.push_back(leftDown);

        INPUT leftUp = leftDown;
        leftUp.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(leftUp);
    }

    INPUT shiftUp = shiftDown;
    shiftUp.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(shiftUp);

    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent == inputs.size()) {
        return true;
    }

    const auto cleanup = TextBridgeInputUtils::PlanPartialSelectionCleanup(
        sent,
        inputs.size()
    );
    if (cleanup.releaseLeft) {
        SendKeyUp(VK_LEFT);
    }
    if (cleanup.releaseShift) {
        SendKeyUp(VK_SHIFT);
    }
    if (cleanup.collapseSelection) {
        SendKey(VK_RIGHT);
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
