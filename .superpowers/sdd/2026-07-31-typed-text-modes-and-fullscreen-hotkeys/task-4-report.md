# Task 4 report: suppress script hotkeys in true fullscreen windows

## RED evidence

After adding `TextMagicFullscreenUtilsTests` and its test source, `rtk cmake -S . -B build` followed by `rtk cmake --build build --config Debug --target TextMagicFullscreenUtilsTests` failed as expected: CMake could not find `src/app/FullscreenUtils.cpp` and the target had no sources.

## GREEN evidence

After implementing the helper, `rtk cmake --build build --config Debug --target TextMagicFullscreenUtilsTests` succeeded and `rtk ctest --test-dir build -C Debug -R TextMagicFullscreenUtilsTests --output-on-failure` passed 1/1. Final verification, `rtk cmake --build build --config Debug` and `rtk ctest --test-dir build -C Debug -R "TextMagic(InputBuffer|FullscreenUtils)Tests" --output-on-failure`, succeeded; both focused tests passed (2/2).

## Files

- Created `src/app/FullscreenUtils.h`, `src/app/FullscreenUtils.cpp`, and `tests/FullscreenUtilsTests.cpp`.
- Updated `CMakeLists.txt`, `src/app/Application.h`, `src/app/Application.cpp`, `lang/en.ini`, and `lang/ru.ini`.

## Self-review

- `IsFullscreenBounds` requires all four monitor bounds and rejects ordinary maximized windows; its test covers exact coverage, over-coverage, work-area maximization, and ordinary maximization.
- The foreground helper rejects null, ignored, desktop, shell, and minimized windows, then uses the nearest monitor and `WS_OVERLAPPEDWINDOW` with `IsZoomed` to exclude ordinary maximized windows.
- Both native and hook hotkeys dispatch through `WM_HOTKEY` to `ExecuteScriptByHotkeyId`; the new early return is there only. UI execution remains on `ExecuteScript(..., true)`, unchanged. The option defaults to false, persists as `0`/`1`, and does not log or change status on suppression.
- `rtk git diff --check` reported no whitespace errors. No executable blacklist or additional scaffolding was added.

## Concerns

None. The desktop-dependent foreground query is intentionally kept behind the pure classifier test; the required behavior is covered without a fragile live-window test.
