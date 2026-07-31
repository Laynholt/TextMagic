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

    Expect(FullscreenUtils::IsFullscreenBounds(fullscreen, monitor, false),
           "exact monitor coverage must be fullscreen");
    Expect(FullscreenUtils::IsFullscreenBounds(covering, monitor, false),
           "covering monitor bounds must be fullscreen");
    Expect(!FullscreenUtils::IsFullscreenBounds(maximizedWorkArea, monitor, false),
           "work-area maximization must not be fullscreen");
    Expect(!FullscreenUtils::IsFullscreenBounds(fullscreen, monitor, true),
           "ordinary maximized windows must not be fullscreen");
    return 0;
}
