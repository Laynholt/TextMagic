#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

class ScriptExecutionGate {
public:
    static constexpr std::uint64_t CooldownMs = 250;

    bool TryReserve(std::uint64_t now) noexcept {
        if (m_reserved || (m_hasRelease && now - m_lastRelease < CooldownMs)) {
            return false;
        }
        m_reserved = true;
        return true;
    }

    void Release(std::uint64_t now) noexcept {
        m_reserved = false;
        m_lastRelease = now;
        m_hasRelease = true;
    }

    bool IsReserved() const noexcept { return m_reserved; }

private:
    bool m_reserved = false;
    bool m_hasRelease = false;
    std::uint64_t m_lastRelease = 0;
};

namespace HotkeyDispatch {
inline void ApplyHookShiftState(BYTE* keyboardState, bool shiftDown) noexcept {
    if (!keyboardState) {
        return;
    }
    keyboardState[VK_SHIFT] = static_cast<BYTE>(
        (keyboardState[VK_SHIFT] & 0x7f) | (shiftDown ? 0x80 : 0));
}

constexpr std::uint32_t NormalizeHookVirtualKey(
    std::uint32_t virtualKey,
    std::uint32_t scanCode,
    std::uint32_t flags
) noexcept {
    if (virtualKey == VK_SHIFT) {
        return scanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    }
    if (virtualKey == VK_CONTROL) {
        return (flags & LLKHF_EXTENDED) != 0 ? VK_RCONTROL : VK_LCONTROL;
    }
    if (virtualKey == VK_MENU) {
        return (flags & LLKHF_EXTENDED) != 0 ? VK_RMENU : VK_LMENU;
    }
    return virtualKey;
}

constexpr bool IsModifierVirtualKey(std::uint32_t virtualKey) noexcept {
    return virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT
        || virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL
        || virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU
        || virtualKey == VK_LWIN || virtualKey == VK_RWIN
        || virtualKey == VK_CAPITAL;
}

class PressedKeyState {
public:
    void Update(std::uint32_t virtualKey, bool pressed) noexcept {
        if (virtualKey < m_pressed.size()) {
            m_pressed[virtualKey] = pressed;
        }
    }

    bool IsPressed(std::uint32_t virtualKey) const noexcept {
        return virtualKey < m_pressed.size() && m_pressed[virtualKey];
    }

    UINT Modifiers() const noexcept {
        const bool control = IsPressed(VK_CONTROL) || IsPressed(VK_LCONTROL) || IsPressed(VK_RCONTROL);
        const bool alt = IsPressed(VK_MENU) || IsPressed(VK_LMENU) || IsPressed(VK_RMENU);
        const bool shift = IsPressed(VK_SHIFT) || IsPressed(VK_LSHIFT) || IsPressed(VK_RSHIFT);
        const bool win = IsPressed(VK_LWIN) || IsPressed(VK_RWIN);
        return (control ? MOD_CONTROL : 0)
            | (alt ? MOD_ALT : 0)
            | (shift ? MOD_SHIFT : 0)
            | (win ? MOD_WIN : 0);
    }

    void Clear() noexcept { m_pressed = {}; }

private:
    std::array<bool, 256> m_pressed{};
};

enum class Action {
    PassThrough,
    Consume,
    Dispatch,
};

constexpr bool MatchesVirtualKey(std::uint32_t inputVirtualKey, std::uint32_t hotkeyVirtualKey) noexcept {
    if (hotkeyVirtualKey == VK_SHIFT) {
        return inputVirtualKey == VK_SHIFT || inputVirtualKey == VK_LSHIFT || inputVirtualKey == VK_RSHIFT;
    }
    if (hotkeyVirtualKey == VK_CONTROL) {
        return inputVirtualKey == VK_CONTROL || inputVirtualKey == VK_LCONTROL || inputVirtualKey == VK_RCONTROL;
    }
    if (hotkeyVirtualKey == VK_MENU) {
        return inputVirtualKey == VK_MENU || inputVirtualKey == VK_LMENU || inputVirtualKey == VK_RMENU;
    }
    if (hotkeyVirtualKey == VK_LWIN || hotkeyVirtualKey == VK_RWIN) {
        return inputVirtualKey == VK_LWIN || inputVirtualKey == VK_RWIN;
    }
    return inputVirtualKey == hotkeyVirtualKey;
}

constexpr Action BeginMatchedPress(bool& armed) noexcept {
    armed = false;
    return Action::Consume;
}

constexpr bool ShouldConsumeHeldRepeat(
    bool armed,
    std::uint32_t inputVirtualKey,
    std::uint32_t hotkeyVirtualKey
) noexcept {
    return !armed && MatchesVirtualKey(inputVirtualKey, hotkeyVirtualKey);
}

constexpr bool RearmOnReleasedKey(
    bool& armed,
    std::uint32_t hotkeyVirtualKey,
    std::uint32_t releasedVirtualKey
) noexcept {
    if (armed || !MatchesVirtualKey(releasedVirtualKey, hotkeyVirtualKey)) {
        return false;
    }
    armed = true;
    return true;
}

constexpr bool ShouldTrackInput(bool blocked) noexcept {
    return !blocked;
}

constexpr bool ShouldRearmBlockedKeyEvent(bool keyUp) noexcept {
    return keyUp;
}

constexpr Action Decide(bool blocked, bool matched, bool reservationSucceeded) noexcept {
    if (blocked || !matched) {
        return Action::PassThrough;
    }
    return reservationSucceeded ? Action::Dispatch : Action::Consume;
}

constexpr Action DecideHeldRepeat(bool heldRepeat, bool matched, bool reservationSucceeded) noexcept {
    return heldRepeat ? Action::Consume : Decide(false, matched, reservationSucceeded);
}
}
