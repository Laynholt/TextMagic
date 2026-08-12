#pragma once

#include <windows.h>

#include <cstdint>

namespace FullscreenUtils {
class ForegroundBlockCache {
public:
    bool Matches(HWND window,
                 std::uint64_t blacklistGeneration,
                 bool fullscreenSetting) const noexcept {
        return m_valid
            && m_window == window
            && m_blacklistGeneration == blacklistGeneration
            && m_fullscreenSetting == fullscreenSetting;
    }

    void Store(HWND window,
               std::uint64_t blacklistGeneration,
               bool fullscreenSetting,
               bool blocked) noexcept {
        m_window = window;
        m_blacklistGeneration = blacklistGeneration;
        m_fullscreenSetting = fullscreenSetting;
        m_blocked = blocked;
        m_valid = true;
    }

    bool Blocked() const noexcept { return m_blocked; }
    void Invalidate() noexcept { m_valid = false; }

private:
    HWND m_window = nullptr;
    std::uint64_t m_blacklistGeneration = 0;
    bool m_fullscreenSetting = false;
    bool m_blocked = false;
    bool m_valid = false;
};

bool IsFullscreenBounds(const RECT& extendedFrameBounds,
                        const RECT& clientBounds,
                        const RECT& monitorRect,
                        bool hasExtendedFrameBounds);
bool IsForegroundWindowFullscreen();
}
