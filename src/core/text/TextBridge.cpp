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

std::wstring TextBridge::GetSelectedText() const {
    return CopyFromActiveControl();
}

bool TextBridge::SetSelectedText(const std::wstring& text) const {
    return PasteIntoActiveControl(text);
}

bool TextBridge::ReplaceText(
    HWND expectedTarget,
    const std::wstring& oldText,
    const std::wstring& replacement
) const {
    const TextBridgeInputUtils::AtomicReplacementOperations operations{
        [expectedTarget]() {
            return IsTargetCurrent(expectedTarget);
        },
        [expectedTarget](size_t deleteCount, const std::wstring& text) {
            const size_t maxCharacters = (std::numeric_limits<UINT>::max)() / 2;
            if (!IsTargetCurrent(expectedTarget)
                || deleteCount > maxCharacters
                || text.size() > maxCharacters - deleteCount) {
                return false;
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
                return true;
            }
            const UINT inputCount = static_cast<UINT>(inputs.size());
            return SendInput(inputCount, inputs.data(), sizeof(INPUT)) == inputCount;
        }
    };
    return TextBridgeInputUtils::RunAtomicReplacement(
        oldText.size(), replacement, operations);
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
    const HWND target = GetForegroundWindow();
    if (!WaitForModifiersRelease(target)) {
        return false;
    }
    return ReplaceText(
        target,
        std::wstring(TextBridgeInputUtils::SelectionDeleteCount(text), L' '),
        text
    );
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
