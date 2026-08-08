#pragma once

#include <windows.h>

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
