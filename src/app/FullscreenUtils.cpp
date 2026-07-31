#include "FullscreenUtils.h"

#include <dwmapi.h>

namespace FullscreenUtils {
bool IsFullscreenBounds(const RECT& extendedFrameBounds,
                        const RECT& clientBounds,
                        const RECT& monitorRect,
                        bool hasExtendedFrameBounds) {
    const RECT& visibleBounds = hasExtendedFrameBounds ? extendedFrameBounds : clientBounds;
    return visibleBounds.left <= monitorRect.left
        && visibleBounds.top <= monitorRect.top
        && visibleBounds.right >= monitorRect.right
        && visibleBounds.bottom >= monitorRect.bottom;
}

bool IsForegroundWindowFullscreen() {
    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow
        || foregroundWindow == GetDesktopWindow()
        || foregroundWindow == GetShellWindow()
        || IsIconic(foregroundWindow)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(foregroundWindow, &processId);
    if (processId == GetCurrentProcessId()) {
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(foregroundWindow, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return false;
    }

    RECT extendedFrameBounds = {};
    const bool hasExtendedFrameBounds = SUCCEEDED(DwmGetWindowAttribute(
        foregroundWindow,
        DWMWA_EXTENDED_FRAME_BOUNDS,
        &extendedFrameBounds,
        sizeof(extendedFrameBounds)
    )) && !IsRectEmpty(&extendedFrameBounds);

    RECT clientBounds = {};
    if (!hasExtendedFrameBounds) {
        if (!GetClientRect(foregroundWindow, &clientBounds)) {
            return false;
        }
        POINT topLeft = { clientBounds.left, clientBounds.top };
        POINT bottomRight = { clientBounds.right, clientBounds.bottom };
        if (!ClientToScreen(foregroundWindow, &topLeft)
            || !ClientToScreen(foregroundWindow, &bottomRight)) {
            return false;
        }
        clientBounds = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    }

    return IsFullscreenBounds(
        extendedFrameBounds,
        clientBounds,
        monitorInfo.rcMonitor,
        hasExtendedFrameBounds
    );
}
}
