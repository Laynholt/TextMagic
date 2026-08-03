#pragma once

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

constexpr bool ShouldTrackInput(bool blocked) noexcept {
    return !blocked;
}

constexpr Action Decide(bool blocked, bool matched, bool reservationSucceeded) noexcept {
    if (blocked || !matched) {
        return Action::PassThrough;
    }
    return reservationSucceeded ? Action::Dispatch : Action::Consume;
}
}
