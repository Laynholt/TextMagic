# Running Picker and Surface Finish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the running-application picker the approved title treatment, remove the remaining light ListView edges, make the Logs rounding visible, and ship the result as TextMagic 1.1.0.

**Architecture:** Keep the changes mode-specific inside the existing Win32 window procedures. Put deterministic running-picker geometry and shared surface constants in the existing pure layout/style headers so TDD can cover the contracts, while reusing the current DWM, scrollbar, header, renderer, and rounded-region helpers.

**Tech Stack:** C++17, Win32/Common Controls, DWM, CMake, CTest, existing TextMagic GDI/GDI+ renderer.

## Global Constraints

- Keep the localized `application_blacklist.running` title text unchanged.
- Running-picker title height is `40 px`; gap below it is `8 px`; use the shared large bold title font.
- Generic MessageWindow title height remains `24 px` with its existing compact font and `6 px` gap.
- Preserve table columns, widths, sorting, selection, scrolling, row colors, commands, and button behavior.
- Preserve Logs typography, colors, selection, copying, context menu, append behavior, scrollbar behavior, and full control rectangle.
- Shared large-content radius remains `10 px`; shared region inset remains `1 px`; Logs region inset is `2 px`.
- The configured project version must be `1.1.0`, generated from CMake without a duplicate production version constant.
- Every shell command starts with `rtk`.
- Use `apply_patch` for edits and follow strict RED -> GREEN -> REFACTOR.

---

### Task 1: Running-picker title geometry and dark native title bar

**Files:**
- Modify: `src/app/InfoWindowLayout.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces: `RunningPickerWindowLayout` with `title`, `list`, `secondaryButton`, and `primaryButton` rectangles.
- Produces: `CalculateRunningPickerWindowLayout(int clientWidth, int clientHeight)`.
- Consumes: existing `InfoRect`, `Application::m_hTitleFont`, `Application::m_hFont`, and `ApplyDarkTitleBar(HWND)`.

- [ ] **Step 1: Add failing geometry assertions**

Add this behavior-focused block to `tests/InfoWindowLayoutTests.cpp` before `return 0`:

```cpp
    const RunningPickerWindowLayout runningPicker =
        CalculateRunningPickerWindowLayout(760, 520);
    Check(runningPicker.title.height == 40,
          "running picker title leaves room for the large heading");
    Check(runningPicker.list.y
              >= runningPicker.title.y + runningPicker.title.height + 8,
          "running picker list follows the approved title gap");
    Check(runningPicker.list.y + runningPicker.list.height + 10
              <= runningPicker.primaryButton.y,
          "running picker list stays above the footer");
    Check(runningPicker.primaryButton.y == runningPicker.secondaryButton.y,
          "running picker buttons share a baseline");
    Check(IsInside(runningPicker.title, 760, 520),
          "running picker title stays inside the client");
    Check(IsInside(runningPicker.list, 760, 520),
          "running picker list stays inside the client");
    Check(IsInside(runningPicker.primaryButton, 760, 520),
          "running picker primary button stays inside the client");
    Check(IsInside(runningPicker.secondaryButton, 760, 520),
          "running picker secondary button stays inside the client");
    Check(info_window_layout_detail::kMessageCompactTitleHeight == 24,
          "generic message title height remains compact");
    Check(info_window_layout_detail::kMessageCompactTitleGap == 6,
          "generic message title gap remains unchanged");
```

The production mutation caught by this test is removal of the running-picker layout/type or restoration of the compact 24/6 geometry for the running picker.

- [ ] **Step 2: Run the focused test to verify RED**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compilation fails because `RunningPickerWindowLayout`, `CalculateRunningPickerWindowLayout`, `kMessageCompactTitleHeight`, and `kMessageCompactTitleGap` do not exist.

- [ ] **Step 3: Add the pure running-picker layout**

In `src/app/InfoWindowLayout.h`, add:

```cpp
struct RunningPickerWindowLayout {
    InfoRect title;
    InfoRect list;
    InfoRect secondaryButton;
    InfoRect primaryButton;
};
```

Add exact constants to `info_window_layout_detail`:

```cpp
constexpr int kMessageOuterInset = 14;
constexpr int kMessageCompactTitleHeight = 24;
constexpr int kMessageCompactTitleGap = 6;
constexpr int kRunningPickerTitleHeight = 40;
constexpr int kRunningPickerTitleGap = 8;
constexpr int kMessageButtonHeight = 34;
constexpr int kMessageButtonGap = 10;
constexpr int kMessageFooterGap = 10;
constexpr int kRunningPickerButtonWidth = 180;
constexpr int kRunningPickerListMinimumHeight = 50;
```

Implement:

```cpp
inline RunningPickerWindowLayout CalculateRunningPickerWindowLayout(
    int clientWidth,
    int clientHeight
) {
    using namespace info_window_layout_detail;
    const int safeWidth = ClientWidth(clientWidth);
    const int safeHeight = NonNegative(clientHeight);
    const InfoRect title{
        kMessageOuterInset,
        kMessageOuterInset,
        NonNegative(safeWidth - 2 * kMessageOuterInset),
        kRunningPickerTitleHeight,
    };
    const int listY = title.y + title.height + kRunningPickerTitleGap;
    const int desiredFooterY = safeHeight - kMessageOuterInset - kMessageButtonHeight;
    const int footerY = std::max(
        listY + kRunningPickerListMinimumHeight + kMessageFooterGap,
        desiredFooterY);
    const InfoRect list{
        kMessageOuterInset,
        listY,
        NonNegative(safeWidth - 2 * kMessageOuterInset),
        std::max(kRunningPickerListMinimumHeight,
                 footerY - kMessageFooterGap - listY),
    };
    const InfoRect primary{
        std::max(kMessageOuterInset,
                 safeWidth - kMessageOuterInset - kRunningPickerButtonWidth),
        footerY,
        std::min(kRunningPickerButtonWidth,
                 NonNegative(safeWidth - 2 * kMessageOuterInset)),
        kMessageButtonHeight,
    };
    const InfoRect secondary{
        std::max(kMessageOuterInset,
                 primary.x - kMessageButtonGap - kRunningPickerButtonWidth),
        footerY,
        std::min(kRunningPickerButtonWidth,
                 NonNegative(primary.x - kMessageButtonGap - kMessageOuterInset)),
        kMessageButtonHeight,
    };
    return {title, list, secondary, primary};
}
```

- [ ] **Step 4: Route only the running-picker mode through the new layout**

In `Application::MessageWindowProc`:

- set `state->titleLabel` to `state->owner->m_hTitleFont` only when `state->runningApplicationSelection`; retain `m_hFont` otherwise;
- in `WM_SIZE`, use `CalculateRunningPickerWindowLayout(w, h)` only for `runningApplicationSelection`, moving the title, ListView, secondary button, and primary button from those rectangles;
- retain the current compact `m=14`, `titleH=24`, `6 px` gap path for generic MessageWindow modes;
- continue applying the rounded child region after moving the ListView.

In `Application::SelectRunningApplications`, after successful `CreateWindowExW` and before `ShowWindow`, call:

```cpp
ApplyDarkTitleBar(messageWindow);
```

- [ ] **Step 5: Verify GREEN and regression coverage**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests TextMagic
rtk ctest --test-dir build -C Release -R "TextMagicInfoWindowLayoutTests" --output-on-failure
rtk git diff --check
```

Expected: focused CTest passes 1/1, both targets build, and diff check is clean.

- [ ] **Step 6: Commit**

```powershell
rtk git add src/app/InfoWindowLayout.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "style: promote running picker heading"
```

---

### Task 2: Remove native light ListView edges and expose Logs rounding

**Files:**
- Modify: `src/app/ContentSurfaceStyle.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces: `content_surface_style::kDefaultRegionInset` (`1`) and `content_surface_style::kLogsRegionInset` (`2`).
- Changes: `ApplyRoundedChildRegion(HWND, int, int, int, int borderInset)` with a default argument preserving existing callers.
- Consumes: `ApplyDarkScrollBar(HWND, bool)`, `ApplyDarkListViewHeader(HWND)`, and the current parent-drawn rounded frames.

- [ ] **Step 1: Add failing surface-contract assertions**

Add to `tests/InfoWindowLayoutTests.cpp`:

```cpp
    Check(content_surface_style::kDefaultRegionInset == 1,
          "shared rounded controls keep their existing inset");
    Check(content_surface_style::kLogsRegionInset == 2,
          "logs expose enough parent border to show rounded corners");
    Check(content_surface_style::kLogsRegionInset
              > content_surface_style::kDefaultRegionInset,
          "logs use a stronger clip without changing shared surfaces");
```

The production mutation caught by this test is collapsing the Logs inset back to the shared value, which recreates the visually square corners.

- [ ] **Step 2: Run the focused test to verify RED**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compilation fails because the two region-inset constants do not exist.

- [ ] **Step 3: Add the scoped inset constants and parameter**

In `src/app/ContentSurfaceStyle.h`, add:

```cpp
constexpr int kDefaultRegionInset = 1;
constexpr int kLogsRegionInset = 2;
```

Change the helper signature in `src/app/Application.cpp` to:

```cpp
void ApplyRoundedChildRegion(
    HWND control,
    int width,
    int height,
    int radius,
    int borderInset = content_surface_style::kDefaultRegionInset
)
```

Remove the local hard-coded inset, clamp the supplied inset to at least zero, retain the small-control fallback, and keep deleting the region only when `SetWindowRgn` fails.

- [ ] **Step 4: Apply the stronger inset only to Logs**

Pass `content_surface_style::kLogsRegionInset` to both Logs calls:

```cpp
ApplyRoundedChildRegion(
    state->logList,
    layout.content.width,
    layout.content.height,
    LOGS_PANEL_CORNER_RADIUS,
    content_surface_style::kLogsRegionInset);
ApplyRoundedChildRegion(
    state->emptyLabel,
    layout.content.width,
    layout.content.height,
    LOGS_PANEL_CORNER_RADIUS,
    content_surface_style::kLogsRegionInset);
```

Do not change their `MoveWindow` rectangles, the renderer fill/border, row drawing, or scrollbar update.

- [ ] **Step 5: Stop requesting Explorer theme for the two ListViews**

Disable native visual styles for the two ListViews before installing the existing dark scrollbar hook, then change exactly these calls:

```cpp
SetWindowTheme(state->blacklistList, L"", L"");
ApplyDarkScrollBar(state->blacklistList, false);
SetWindowTheme(state->textControl, L"", L"");
ApplyDarkScrollBar(state->textControl, false);
```

The second pair is only in the `runningApplicationSelection` ListView branch. Disabling the native theme suppresses the light non-client edge; the existing application-painted header and outer rounded frame remain authoritative. Keep `ApplyDarkListViewHeader`, dark-mode enablement, scrollbar subclassing, colors, styles, columns, and sorting unchanged. The generic MessageWindow ListBox and Logs ListBox continue using the default Explorer-theme argument.

- [ ] **Step 6: Verify GREEN and regression coverage**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests TextMagic
rtk ctest --test-dir build -C Release -R "TextMagicInfoWindowLayoutTests" --output-on-failure
rtk git diff --check
```

Expected: focused CTest passes 1/1, both targets build, and diff check is clean.

- [ ] **Step 7: Commit**

```powershell
rtk git add src/app/ContentSurfaceStyle.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "fix: finish dark content surface edges"
```

---

### Task 3: Generate and verify version 1.1.0, then rebuild Release

**Files:**
- Modify: `CMakeLists.txt`
- Create: `tests/AppVersionTests.cpp`

**Interfaces:**
- Consumes: generated `AppVersion.h` macros.
- Produces: `TextMagicAppVersionTests` CTest target.
- Produces: project/runtime/resource version `1.1.0` and Windows file-version tuple `1,1,0,0`.

- [ ] **Step 1: Add the failing generated-version test and test target**

Create `tests/AppVersionTests.cpp`:

```cpp
#include "AppVersion.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    Check(std::wstring_view(TM_APP_VERSION_W) == L"1.1.0",
          "runtime version is generated as 1.1.0");
    Check(TM_VERSION_MAJOR == 1 && TM_VERSION_MINOR == 1 && TM_VERSION_PATCH == 0,
          "numeric version components match 1.1.0");
    Check(TM_VERSION_MAJOR == 1 && TM_VERSION_MINOR == 1
              && TM_VERSION_PATCH == 0 && TM_VERSION_BUILD == 0,
          "Windows file version tuple matches 1.1.0.0");
    return 0;
}
```

In `CMakeLists.txt`, add the target without changing `project(... VERSION 1.0.0)` yet:

```cmake
add_executable(TextMagicAppVersionTests
    tests/AppVersionTests.cpp
)

target_include_directories(TextMagicAppVersionTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(TextMagicAppVersionTests PRIVATE cxx_std_17)
add_test(NAME TextMagicAppVersionTests COMMAND TextMagicAppVersionTests)
```

- [ ] **Step 2: Configure, build, and verify RED**

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Release --target TextMagicAppVersionTests
rtk ctest --test-dir build -C Release -R "TextMagicAppVersionTests" --output-on-failure
```

Expected: target builds, CTest fails because generated `TM_APP_VERSION_W` is still `1.0.0` and numeric minor is still `0`.

- [ ] **Step 3: Bump the single project version source**

Change only the active CMake project version:

```cmake
project(TextMagic
    VERSION 1.1.0
```

Do not hard-code the version in `Application.h`, `resource.rc`, or update-service production code.

- [ ] **Step 4: Configure and verify GREEN**

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Release --target TextMagicAppVersionTests
rtk ctest --test-dir build -C Release -R "TextMagicAppVersionTests" --output-on-failure
```

Expected: CTest passes 1/1.

- [ ] **Step 5: Rebuild the complete Release and run all tests**

Run:

```powershell
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: Release build succeeds, all tests pass (the suite count increases by one), and diff check is clean. Confirm `build/bin/Release/TextMagic.exe` has a fresh timestamp.

- [ ] **Step 6: Attempt safe native verification**

Launch only the newly built executable and verify, if the Windows capture/input harness is available:

- running picker has black native title bar and a large bold in-card title;
- both table surfaces have no light native top/side edge;
- Logs content has visibly rounded corners with and without its scrollbar;
- running picker sorting, multi-selection, cancel/add, and Logs selection/copy still work.

If capture fails with the known `SetIsBorderRequired ... 0x80004002` limitation, stop UI automation safely, close only the exact launched process, and record the visual gap rather than claiming success.

- [ ] **Step 7: Commit**

```powershell
rtk git add CMakeLists.txt tests/AppVersionTests.cpp
rtk git commit -m "release: bump version to 1.1.0"
```

---

## Final Review and Integration

- [ ] Generate one whole-branch review package from the branch base through HEAD.
- [ ] Dispatch a fresh final reviewer to check the full spec, task reports, deferred findings, behavior preservation, GDI/region ownership, test evidence, and scope.
- [ ] If findings exist, dispatch one fix agent for the complete finding set, run one scoped re-review, and rerun covering tests.
- [ ] Run a final fresh `rtk cmake --build build --config Release`, full `rtk ctest --test-dir build -C Release --output-on-failure`, `rtk git diff --check`, and `rtk git status --short`.
- [ ] Integrate the reviewed branch to `master` using the finishing-development-branch workflow and rebuild `F:\Data\Code\C++\TextMagic\build\bin\Release\TextMagic.exe` on `master`.
