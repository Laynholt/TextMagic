#include "TextBridge.h"

#include "ClipboardUtils.h"
#include "TextBridgeInputUtils.h"

#include <cwchar>
#include <uiautomation.h>
#include <wrl/client.h>

namespace {
constexpr int TARGET_INPUT_IDLE_TIMEOUT_MS = 1000;

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
    if (!TextBridgeInputUtils::ShouldSelectBeforeDelete(count)) {
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
        return SendKeyUp(virtualKey);
    }
    return sent == 2;
}

bool TextBridge::SendKeyDown(WORD virtualKey) const {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
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
    const HWND targetWindow = GetForegroundWindow();
    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    Microsoft::WRL::ComPtr<IUIAutomationElement> targetElement;
    Microsoft::WRL::ComPtr<IUIAutomationWindowPattern> targetWindowPattern;
    if (!targetWindow
        || FAILED(CoCreateInstance(
            __uuidof(CUIAutomation),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IUIAutomation),
            reinterpret_cast<void**>(automation.GetAddressOf())
        ))
        || FAILED(automation->ElementFromHandle(targetWindow, targetElement.GetAddressOf()))
        || FAILED(targetElement->GetCurrentPatternAs(
            UIA_WindowPatternId,
            __uuidof(IUIAutomationWindowPattern),
            reinterpret_cast<void**>(targetWindowPattern.GetAddressOf())
        ))) {
        return false;
    }

    if (!SendKeyDown(VK_SHIFT)) {
        return false;
    }

    const auto failSelection = [this](bool leftMayBeDown, bool hasSelection) {
        bool leftReleased = !leftMayBeDown || SendKeyUp(VK_LEFT);
        if (!leftReleased) {
            leftReleased = SendKeyUp(VK_LEFT);
        }
        bool shiftReleased = SendKeyUp(VK_SHIFT);
        if (!shiftReleased) {
            shiftReleased = SendKeyUp(VK_SHIFT);
        }
        if (leftReleased && shiftReleased && hasSelection && !SendKey(VK_RIGHT)) {
            SendKeyUp(VK_RIGHT);
        }
        return false;
    };

    size_t selected = 0;
    for (; selected < count; ++selected) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_LEFT;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        const UINT sent = SendInput(2, inputs, sizeof(INPUT));
        if (sent != 2) {
            return failSelection(sent > 0, selected > 0 || sent > 0);
        }
        if (TextBridgeInputUtils::ShouldWaitForSelectionConsumption(selected + 1, count)) {
            BOOL targetIdle = FALSE;
            if (FAILED(targetWindowPattern->WaitForInputIdle(
                    TARGET_INPUT_IDLE_TIMEOUT_MS,
                    &targetIdle
                ))
                || !targetIdle) {
                return failSelection(false, true);
            }
        }
    }

    if (!SendKeyUp(VK_SHIFT)) {
        return failSelection(false, true);
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
