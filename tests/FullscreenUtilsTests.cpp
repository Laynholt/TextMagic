#include "FullscreenUtils.h"

#include <cstdint>
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

    FullscreenUtils::ForegroundBlockCache cache;
    const HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1));
    const HWND otherWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(2));
    constexpr std::uint64_t generation = 7;
    constexpr bool fullscreenSetting = true;
    FullscreenUtils::WindowStateToken windowState;
    windowState.processId = 100;
    windowState.threadId = 200;
    windowState.classAtom = 300;
    windowState.style = WS_VISIBLE;
    windowState.windowBounds = { 0, 0, 1920, 1040 };
    windowState.clientBounds = { 0, 0, 1920, 1040 };
    windowState.monitorBounds = { 0, 0, 1920, 1080 };

    Expect(!cache.Matches(window, generation, fullscreenSetting, windowState),
           "an empty foreground block cache must miss");
    cache.Store(window, generation, fullscreenSetting, windowState, true);
    Expect(cache.Matches(window, generation, fullscreenSetting, windowState),
           "an unchanged foreground block decision must be reusable");
    Expect(cache.Blocked(), "the cache must retain the final blocked result");
    Expect(!cache.Matches(otherWindow, generation, fullscreenSetting, windowState),
           "a foreground window change must invalidate the cached decision");
    Expect(!cache.Matches(window, generation + 1, fullscreenSetting, windowState),
           "a blacklist change must invalidate the cached decision");
    Expect(!cache.Matches(window, generation, !fullscreenSetting, windowState),
           "a fullscreen setting change must invalidate the cached decision");

    FullscreenUtils::WindowStateToken fullscreenState = windowState;
    fullscreenState.windowBounds = { 0, 0, 1920, 1080 };
    fullscreenState.clientBounds = { 0, 0, 1920, 1080 };
    Expect(!cache.Matches(window, generation, fullscreenSetting, fullscreenState),
           "a same-window fullscreen transition must invalidate the cached decision");

    FullscreenUtils::WindowStateToken changedMonitorState = windowState;
    changedMonitorState.monitorBounds = { 0, 0, 1920, 1040 };
    Expect(!cache.Matches(window, generation, fullscreenSetting, changedMonitorState),
           "a monitor geometry change must invalidate the cached decision");

    FullscreenUtils::WindowStateToken reusedWindowState = windowState;
    reusedWindowState.processId = 101;
    Expect(!cache.Matches(window, generation, fullscreenSetting, reusedWindowState),
           "a reused window handle from another process must invalidate the cached decision");

    cache.Invalidate();
    Expect(!cache.Matches(window, generation, fullscreenSetting, windowState),
           "an explicitly invalidated foreground block cache must miss");

    const HHOOK keyboardHook = reinterpret_cast<HHOOK>(static_cast<std::uintptr_t>(3));
    Expect(FullscreenUtils::KeyboardHookAfterRollback(keyboardHook, false) == keyboardHook,
           "a failed keyboard unhook must preserve the handle for shutdown retry");
    Expect(FullscreenUtils::KeyboardHookAfterRollback(keyboardHook, true) == nullptr,
           "a successful keyboard unhook must clear the installed handle");
    return 0;
}
