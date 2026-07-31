#pragma once

#include <windows.h>

namespace FullscreenUtils {
bool IsFullscreenBounds(const RECT& extendedFrameBounds,
                        const RECT& clientBounds,
                        const RECT& monitorRect,
                        bool hasExtendedFrameBounds);
bool IsForegroundWindowFullscreen();
}
