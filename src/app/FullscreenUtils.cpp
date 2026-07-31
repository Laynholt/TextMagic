#include "FullscreenUtils.h"

namespace FullscreenUtils {
bool IsFullscreenBounds(const RECT& windowRect,
                        const RECT& monitorRect,
                        bool ordinaryMaximized) {
    return !ordinaryMaximized
        && windowRect.left <= monitorRect.left
        && windowRect.top <= monitorRect.top
        && windowRect.right >= monitorRect.right
        && windowRect.bottom >= monitorRect.bottom;
}

bool IsForegroundWindowFullscreen(HWND ignoredWindow) {
    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow
        || foregroundWindow == ignoredWindow
        || foregroundWindow == GetDesktopWindow()
        || foregroundWindow == GetShellWindow()
        || IsIconic(foregroundWindow)) {
        return false;
    }

    RECT windowRect = {};
    if (!GetWindowRect(foregroundWindow, &windowRect)) {
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(foregroundWindow, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return false;
    }

    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(foregroundWindow, GWL_STYLE));
    const bool ordinaryMaximized = IsZoomed(foregroundWindow)
        && (style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW;
    return IsFullscreenBounds(windowRect, monitorInfo.rcMonitor, ordinaryMaximized);
}
}
