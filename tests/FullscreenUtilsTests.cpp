#include "FullscreenUtils.h"

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}
}

int main() {
    const RECT monitor = { 0, 0, 1920, 1080 };
    const RECT fullscreen = { 0, 0, 1920, 1080 };
    const RECT maximizedWorkArea = { 0, 0, 1920, 1040 };
    const RECT covering = { -1, -1, 1921, 1081 };
    const RECT framedClient = { 8, 31, 1912, 1072 };
    const RECT restoredVisibleFrame = { 8, 8, 1912, 1072 };

    Expect(FullscreenUtils::IsFullscreenBounds(fullscreen, fullscreen, monitor, true),
           "exact monitor coverage must be fullscreen");
    Expect(FullscreenUtils::IsFullscreenBounds(covering, fullscreen, monitor, true),
           "covering monitor bounds must be fullscreen");
    Expect(!FullscreenUtils::IsFullscreenBounds(
               maximizedWorkArea, maximizedWorkArea, monitor, true),
           "work-area maximization must not be fullscreen");
    Expect(FullscreenUtils::IsFullscreenBounds(fullscreen, framedClient, monitor, true),
           "monitor-covering fullscreen must not be rejected for retained window styles");
    Expect(!FullscreenUtils::IsFullscreenBounds(
               restoredVisibleFrame, framedClient, monitor, true),
           "visible restored frame must not be fullscreen because outer borders cover");
    Expect(!FullscreenUtils::IsFullscreenBounds(covering, framedClient, monitor, false),
           "client fallback must avoid invisible resize-border false positives");
    Expect(FullscreenUtils::IsFullscreenBounds(covering, fullscreen, monitor, false),
           "client fallback must accept a true monitor-covering client");
    return 0;
}
