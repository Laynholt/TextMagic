#include "TextBridge.h"

#include "ClipboardUtils.h"
#include "TextBridgeInputUtils.h"

#include <cwchar>
#include <limits>
#include <vector>

namespace {
bool IsModifierPressed(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool IsTargetCurrent(HWND expectedTarget) {
    return expectedTarget && GetForegroundWindow() == expectedTarget;
}
}

bool TextBridge::WaitForModifiersRelease(HWND expectedTarget, int maxAttempts) const {
    const TextBridgeInputUtils::ModifierWaitOperations operations{
        [expectedTarget]() {
            return !expectedTarget || IsTargetCurrent(expectedTarget);
        },
        []() {
            return IsModifierPressed(VK_CONTROL)
                || IsModifierPressed(VK_MENU)
                || IsModifierPressed(VK_SHIFT)
                || IsModifierPressed(VK_LWIN)
                || IsModifierPressed(VK_RWIN);
        },
        []() { Sleep(5); }
    };
    return TextBridgeInputUtils::WaitForModifiersRelease(maxAttempts, operations);
}

std::wstring TextBridge::GetSelectedText(HWND expectedTarget) const {
    return CopyFromActiveControl(expectedTarget);
}

bool TextBridge::SetSelectedText(HWND expectedTarget, const std::wstring& text) const {
    return PasteIntoActiveControl(expectedTarget, text);
}

bool TextBridge::ReplaceText(
    HWND expectedTarget,
    const std::wstring& oldText,
    const std::wstring& replacement
) const {
    const TextBridgeInputUtils::ReplacementOperations operations{
        [expectedTarget]() {
            return IsTargetCurrent(expectedTarget);
        },
        [expectedTarget](size_t deleteCount, const std::wstring& text) {
            const size_t maxCharacters = (std::numeric_limits<UINT>::max)() / 2;
            if (!IsTargetCurrent(expectedTarget)
                || deleteCount > maxCharacters
                || text.size() > maxCharacters - deleteCount) {
                return TextBridgeInputUtils::InputBatchResult::NotSent;
            }

            std::vector<INPUT> inputs;
            inputs.reserve((deleteCount + text.size()) * 2);
            const auto appendKey = [&inputs](WORD virtualKey) {
                INPUT down = {};
                down.type = INPUT_KEYBOARD;
                down.ki.wVk = virtualKey;
                INPUT up = down;
                up.ki.dwFlags = KEYEVENTF_KEYUP;
                inputs.push_back(down);
                inputs.push_back(up);
            };

            for (size_t i = 0; i < deleteCount; ++i) {
                appendKey(VK_BACK);
            }
            for (size_t i = 0; i < text.size(); ++i) {
                const wchar_t ch = text[i];
                if (ch == L'\r') {
                    if (i + 1 < text.size() && text[i + 1] == L'\n') {
                        ++i;
                    }
                    appendKey(VK_RETURN);
                } else if (ch == L'\n') {
                    appendKey(VK_RETURN);
                } else if (ch == L'\t') {
                    appendKey(VK_TAB);
                } else {
                    INPUT down = {};
                    down.type = INPUT_KEYBOARD;
                    down.ki.wScan = ch;
                    down.ki.dwFlags = KEYEVENTF_UNICODE;
                    INPUT up = down;
                    up.ki.dwFlags |= KEYEVENTF_KEYUP;
                    inputs.push_back(down);
                    inputs.push_back(up);
                }
            }

            if (inputs.empty()) {
                return TextBridgeInputUtils::InputBatchResult::Complete;
            }
            const UINT inputCount = static_cast<UINT>(inputs.size());
            const UINT sentCount = SendInput(inputCount, inputs.data(), sizeof(INPUT));
            if (sentCount == inputCount) {
                return TextBridgeInputUtils::InputBatchResult::Complete;
            }
            if (sentCount > 0
                && (inputs[sentCount - 1].ki.dwFlags & KEYEVENTF_KEYUP) == 0) {
                INPUT keyUp = inputs[sentCount - 1];
                keyUp.ki.dwFlags |= KEYEVENTF_KEYUP;
                SendInput(1, &keyUp, sizeof(INPUT));
            }
            return sentCount == 0
                ? TextBridgeInputUtils::InputBatchResult::NotSent
                : TextBridgeInputUtils::InputBatchResult::Partial;
        },
        [this, expectedTarget]() {
            return SendCtrlShortcut(expectedTarget, 'Z');
        }
    };
    return TextBridgeInputUtils::RunRecoverableReplacement(
        oldText.size(), replacement, operations);
}

std::wstring TextBridge::CopyFromActiveControl(HWND expectedTarget) const {
    if (!IsTargetCurrent(expectedTarget)) {
        return L"";
    }
    ClipboardUtils::Snapshot snapshot;
    if (!snapshot.IsComplete()) {
        return L"";
    }

    if (!WaitForModifiersRelease(expectedTarget)) {
        return L"";
    }

    std::wstring copied;
    if (TryCopyShortcut(expectedTarget, 12, 5, &copied)
        && IsTargetCurrent(expectedTarget)) {
        return copied;
    }
    return L"";
}

bool TextBridge::PasteIntoActiveControl(HWND expectedTarget, const std::wstring& text) const {
    if (!WaitForModifiersRelease(expectedTarget)) {
        return false;
    }
    return ReplaceText(
        expectedTarget,
        std::wstring(TextBridgeInputUtils::SelectionDeleteCount(text), L' '),
        text
    );
}

bool TextBridge::TryCopyShortcut(HWND expectedTarget,
                                 int waitAttempts,
                                 int waitSleepMs,
                                 std::wstring* copied) const {
    if (copied) {
        copied->clear();
    }

    const DWORD sequenceBefore = GetClipboardSequenceNumber();
    std::wstring clipboardBefore;
    const bool hadClipboardText = ClipboardUtils::ReadText(nullptr, &clipboardBefore);
    if (!SendCtrlShortcut(expectedTarget, 'C')) {
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

bool TextBridge::SendCtrlShortcut(HWND expectedTarget, WORD virtualKey) const {
    if (!IsTargetCurrent(expectedTarget)) {
        return false;
    }
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

    return SendInput(4, inputs, sizeof(INPUT)) == 4
        && IsTargetCurrent(expectedTarget);
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
