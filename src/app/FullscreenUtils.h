#pragma once

#include <windows.h>

#include <cstdint>

namespace FullscreenUtils {
struct WindowStateToken {
    DWORD processId = 0;
    DWORD threadId = 0;
    ULONG_PTR classAtom = 0;
    LONG_PTR style = 0;
    RECT windowBounds = {};
    RECT clientBounds = {};
    RECT monitorBounds = {};
};

inline bool operator==(const WindowStateToken& left, const WindowStateToken& right) noexcept {
    return left.processId == right.processId
        && left.threadId == right.threadId
        && left.classAtom == right.classAtom
        && left.style == right.style
        && EqualRect(&left.windowBounds, &right.windowBounds)
        && EqualRect(&left.clientBounds, &right.clientBounds)
        && EqualRect(&left.monitorBounds, &right.monitorBounds);
}

inline HHOOK KeyboardHookAfterRollback(HHOOK keyboardHook, bool unhooked) noexcept {
    return unhooked ? nullptr : keyboardHook;
}

class ForegroundBlockCache {
public:
    bool Matches(HWND window,
                 std::uint64_t blacklistGeneration,
                 bool fullscreenSetting,
                 const WindowStateToken& windowState) const noexcept {
        return m_valid
            && m_window == window
            && m_blacklistGeneration == blacklistGeneration
            && m_fullscreenSetting == fullscreenSetting
            && m_windowState == windowState;
    }

    void Store(HWND window,
               std::uint64_t blacklistGeneration,
               bool fullscreenSetting,
               const WindowStateToken& windowState,
               bool blocked) noexcept {
        m_window = window;
        m_blacklistGeneration = blacklistGeneration;
        m_fullscreenSetting = fullscreenSetting;
        m_windowState = windowState;
        m_blocked = blocked;
        m_valid = true;
    }

    bool Blocked() const noexcept { return m_blocked; }
    void Invalidate() noexcept { m_valid = false; }

private:
    HWND m_window = nullptr;
    std::uint64_t m_blacklistGeneration = 0;
    bool m_fullscreenSetting = false;
    WindowStateToken m_windowState;
    bool m_blocked = false;
    bool m_valid = false;
};

bool IsFullscreenBounds(const RECT& extendedFrameBounds,
                        const RECT& clientBounds,
                        const RECT& monitorRect,
                        bool hasExtendedFrameBounds);
bool IsForegroundWindowFullscreen();
}
