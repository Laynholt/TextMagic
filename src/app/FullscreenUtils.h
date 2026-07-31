#pragma once

#include <windows.h>

namespace FullscreenUtils {
bool IsFullscreenBounds(const RECT& windowRect,
                        const RECT& monitorRect,
                        bool ordinaryMaximized);
bool IsForegroundWindowFullscreen(HWND ignoredWindow);
}
