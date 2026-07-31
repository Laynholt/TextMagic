#include "TextBridge.h"

#include "ClipboardUtils.h"
#include "TextBridgeInputUtils.h"

#include <cwchar>
#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>

namespace {
constexpr DWORD SELECTION_ACK_TIMEOUT_MS = 1000;
constexpr DWORD SELECTION_ACK_POLL_MS = 5;

bool IsModifierPressed(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool IsTargetCurrent(HWND expectedTarget) {
    return expectedTarget && GetForegroundWindow() == expectedTarget;
}

bool IsFocusedElementCurrent(
    IUIAutomation* automation,
    IUIAutomationElement* expectedElement
) {
    Microsoft::WRL::ComPtr<IUIAutomationElement> currentElement;
    BOOL sameElement = FALSE;
    return automation
        && expectedElement
        && SUCCEEDED(automation->GetFocusedElement(currentElement.GetAddressOf()))
        && currentElement
        && SUCCEEDED(automation->CompareElements(
            expectedElement,
            currentElement.Get(),
            &sameElement
        ))
        && sameElement;
}

bool ReadSelectedText(IUIAutomationTextPattern* textPattern, std::wstring* text) {
    if (!textPattern || !text) {
        return false;
    }
    text->clear();

    Microsoft::WRL::ComPtr<IUIAutomationTextRangeArray> ranges;
    if (FAILED(textPattern->GetSelection(ranges.GetAddressOf())) || !ranges) {
        return false;
    }

    int rangeCount = 0;
    if (FAILED(ranges->get_Length(&rangeCount)) || rangeCount != 1) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUIAutomationTextRange> range;
    if (FAILED(ranges->GetElement(0, range.GetAddressOf())) || !range) {
        return false;
    }

    BSTR selectedText = nullptr;
    const HRESULT result = range->GetText(-1, &selectedText);
    if (FAILED(result)) {
        return false;
    }
    if (selectedText) {
        text->assign(selectedText, SysStringLen(selectedText));
        SysFreeString(selectedText);
    }
    return true;
}

}

bool TextBridge::WaitForModifiersRelease(HWND expectedTarget) const {
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (expectedTarget && !IsTargetCurrent(expectedTarget)) {
            return false;
        }
        const bool controlDown = IsModifierPressed(VK_CONTROL);
        const bool altDown = IsModifierPressed(VK_MENU);
        const bool shiftDown = IsModifierPressed(VK_SHIFT);
        const bool lwinDown = IsModifierPressed(VK_LWIN);
        const bool rwinDown = IsModifierPressed(VK_RWIN);
        if (!controlDown && !altDown && !shiftDown && !lwinDown && !rwinDown) {
            return !expectedTarget || IsTargetCurrent(expectedTarget);
        }
        Sleep(5);
        if (expectedTarget && !IsTargetCurrent(expectedTarget)) {
            return false;
        }
    }
    return false;
}

std::wstring TextBridge::GetSelectedText() const {
    return CopyFromActiveControl();
}

bool TextBridge::SetSelectedText(const std::wstring& text) const {
    return PasteIntoActiveControl(text);
}

bool TextBridge::TypeText(
    HWND expectedTarget,
    const std::wstring& text,
    size_t* typedChars
) const {
    if (typedChars) {
        *typedChars = 0;
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (!IsTargetCurrent(expectedTarget)) {
            return false;
        }
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
    return IsTargetCurrent(expectedTarget);
}

bool TextBridge::DeleteCharacters(
    HWND expectedTarget,
    const std::wstring& text
) const {
    const size_t count = text.size();
    if (count == 0) {
        return IsTargetCurrent(expectedTarget);
    }
    if (!IsTargetCurrent(expectedTarget)) {
        return false;
    }
    if (!TextBridgeInputUtils::ShouldSelectBeforeDelete(count)) {
        return SendRepeatedKey(expectedTarget, VK_BACK, count);
    }
    if (!SelectPreviousCharacters(expectedTarget, text)) {
        return false;
    }
    if (IsTargetCurrent(expectedTarget) && SendKey(VK_BACK)) {
        return true;
    }
    if (IsTargetCurrent(expectedTarget)) {
        SendKey(VK_RIGHT);
    }
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

bool TextBridge::SendRepeatedKey(
    HWND expectedTarget,
    WORD virtualKey,
    size_t count
) const {
    for (size_t i = 0; i < count; ++i) {
        if (!IsTargetCurrent(expectedTarget) || !SendKey(virtualKey)) {
            return false;
        }
    }
    return IsTargetCurrent(expectedTarget);
}

bool TextBridge::SelectPreviousCharacters(
    HWND expectedTarget,
    const std::wstring& text
) const {
    if (text.empty() || !IsTargetCurrent(expectedTarget)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    Microsoft::WRL::ComPtr<IUIAutomationElement> focusedElement;
    Microsoft::WRL::ComPtr<IUIAutomationTextPattern> textPattern;
    if (FAILED(CoCreateInstance(
            __uuidof(CUIAutomation),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IUIAutomation),
            reinterpret_cast<void**>(automation.GetAddressOf())
        ))
        || !automation
        || FAILED(automation->GetFocusedElement(focusedElement.GetAddressOf()))
        || !focusedElement
        || FAILED(focusedElement->GetCurrentPatternAs(
            UIA_TextPatternId,
            __uuidof(IUIAutomationTextPattern),
            reinterpret_cast<void**>(textPattern.GetAddressOf())
        ))
        || !textPattern) {
        return false;
    }

    std::wstring initialSelection;
    if (!ReadSelectedText(textPattern.Get(), &initialSelection)
        || !initialSelection.empty()
        || !IsTargetCurrent(expectedTarget)
        || !IsFocusedElementCurrent(automation.Get(), focusedElement.Get())) {
        return false;
    }

    const auto waitForSelection = [&](size_t selected) {
        const std::wstring expectedSelection =
            text.substr(text.size() - selected);
        const ULONGLONG started = GetTickCount64();

        while (true) {
            if (!IsTargetCurrent(expectedTarget)
                || !IsFocusedElementCurrent(automation.Get(), focusedElement.Get())) {
                return false;
            }

            std::wstring currentSelection;
            if (ReadSelectedText(textPattern.Get(), &currentSelection)
                && currentSelection == expectedSelection) {
                return IsTargetCurrent(expectedTarget);
            }

            if (GetTickCount64() - started >= SELECTION_ACK_TIMEOUT_MS) {
                return false;
            }
            Sleep(SELECTION_ACK_POLL_MS);
            if (!IsTargetCurrent(expectedTarget)) {
                return false;
            }
        }
    };

    const TextBridgeInputUtils::SelectionOperations operations = {
        [expectedTarget]() {
            return IsTargetCurrent(expectedTarget);
        },
        [this, expectedTarget]() {
            return IsTargetCurrent(expectedTarget) && SendKeyDown(VK_SHIFT);
        },
        [expectedTarget]() -> size_t {
            if (!IsTargetCurrent(expectedTarget)) {
                return 0;
            }
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_LEFT;
            inputs[1] = inputs[0];
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            return SendInput(2, inputs, sizeof(INPUT));
        },
        waitForSelection,
        [this]() {
            return SendKeyUp(VK_LEFT);
        },
        [this]() {
            return SendKeyUp(VK_SHIFT);
        },
        [this, expectedTarget]() {
            return IsTargetCurrent(expectedTarget) && SendKey(VK_RIGHT);
        }
    };

    if (!TextBridgeInputUtils::RunLongSelection(text.size(), operations)) {
        return false;
    }
    if (!waitForSelection(text.size())) {
        if (IsTargetCurrent(expectedTarget)) {
            SendKey(VK_RIGHT);
        }
        return false;
    }
    return IsTargetCurrent(expectedTarget);
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
