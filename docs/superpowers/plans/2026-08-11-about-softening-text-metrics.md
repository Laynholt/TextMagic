# About Softening and Text Metrics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make About metadata visually softer and eliminate clipped lower glyph portions in About labels and the main `TextMagic` heading without changing Logs.

**Architecture:** Keep About geometry in `InfoWindowLayout.h` and extract the main header's deterministic vertical offsets into a small header-only `MainWindowLayout.h`. `Application.cpp` consumes both layouts and owns native font/brush/paint behavior; existing standalone layout tests cover the exact metrics without requiring a GUI.

**Tech Stack:** C++17, Win32/GDI, CMake/MSBuild, CTest, existing `UiRenderer` helpers.

## Global Constraints

- Logs is accepted and must not change.
- Version is a 132-by-26 compact secondary chip with fill/border `RGB(48, 48, 50)`, label `RGB(160, 160, 165)`, and value `RGB(205, 205, 210)`.
- About information-card fill is `RGB(42, 42, 44)`; its border and divider are `RGB(55, 55, 58)`.
- About row-label height is 24, ordinary value height is 26, details-card height is 174, and derived minimum client height is 428.
- Main title font stays `-26` semibold Segoe UI; its control height becomes 40, hint offset becomes `innerY + 52`, and list-top offset becomes `innerY + 108`.
- Preserve runtime localization, version/count/directory refresh, directory copying, update action, footer buttons, main buttons, status card, and DPI-aware minimum sizing.
- Do not change Logs, main-window fonts, script-list typography, update logic, localization wording, or Windows display scaling.
- Work directly on `master`, as explicitly approved by the user for this feature series.

---

### Task 1: Soften About metadata and fix deterministic text line boxes

**Files:**
- Create: `src/app/MainWindowLayout.h`
- Modify: `src/app/InfoWindowLayout.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Consumes: `AboutWindowLayout`, `CalculateAboutWindowLayout(int, int)`, `InfoWindowState::supportingFont`, `Application::OnResize(int, int)`, and existing About control handles.
- Produces: `MainWindowHeaderLayout CalculateMainWindowHeaderLayout(int innerY)`, refined About layout metrics, compact version-chip paint geometry, and unclipped main/About line boxes.

- [ ] **Step 1: Add failing About metric assertions**

Extend the existing About assertions with:

```cpp
Check(about.versionLine.width == 132,
      "about version chip stays compact");
Check(about.versionLine.height == 26,
      "about version chip reserves a complete text line");
Check(about.detailsPanel.height == 174,
      "about softened card reserves expanded row heights");
Check(about.loadedScriptsLabel.height == 24,
      "about loaded label leaves descender space");
Check(about.loadedScriptsValue.height == 26,
      "about loaded value leaves descender space");
Check(about.directoryLabel.height == 24,
      "about directory label leaves descender space");
Check(info_window_layout_detail::kAboutMinimumClientHeight == 428,
      "about minimum client height derives from expanded sections");
```

Keep the current containment and footer non-overlap assertions.

- [ ] **Step 2: Add the red main-header layout test**

Add this include to `tests/InfoWindowLayoutTests.cpp`:

```cpp
#include "../src/app/MainWindowLayout.h"
```

Then add:

```cpp
const MainWindowHeaderLayout mainHeader =
    CalculateMainWindowHeaderLayout(100);
Check(mainHeader.titleY == 106,
      "main title keeps its top inset");
Check(mainHeader.titleHeight == 40,
      "main title leaves room for descenders");
Check(mainHeader.hintY == 152,
      "main hint follows the expanded title");
Check(mainHeader.hintHeight == 48,
      "main hint height remains unchanged");
Check(mainHeader.listTop == 208,
      "main list preserves its gap below the shifted hint");
Check(mainHeader.titleY + mainHeader.titleHeight
          <= mainHeader.hintY,
      "main title does not overlap hint");
Check(mainHeader.hintY + mainHeader.hintHeight
          <= mainHeader.listTop,
      "main hint does not overlap list");
```

- [ ] **Step 3: Run the focused test and confirm the red state**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compilation fails because `MainWindowLayout.h` does not exist; after temporarily adding only the include target, the existing About values also fail the new 132/26/174/24/26/428 assertions.

- [ ] **Step 4: Create the main header layout helper**

Create `src/app/MainWindowLayout.h` with:

```cpp
#pragma once

struct MainWindowHeaderLayout {
    int titleY;
    int titleHeight;
    int hintY;
    int hintHeight;
    int listTop;
};

inline MainWindowHeaderLayout CalculateMainWindowHeaderLayout(int innerY) {
    return {
        innerY + 6,
        40,
        innerY + 52,
        48,
        innerY + 108,
    };
}
```

This helper carries geometry only and has no Win32 dependency.

- [ ] **Step 5: Update About constants and derive its minimum client height**

In `info_window_layout_detail`, use:

```cpp
constexpr int kAboutDescriptionHeight = 40;
constexpr int kAboutVersionLineHeight = 26;
constexpr int kAboutDetailsHeight = 174;
constexpr int kAboutRowLabelHeight = 24;
constexpr int kAboutValueHeight = 26;
constexpr int kAboutPathHeight = 40;
constexpr int kAboutDividerHeight = 1;
constexpr int kAboutMinimumClientHeight = kOuterInset
    + kTitleHeight
    + kContentInset
    + kAboutDescriptionHeight
    + kContentInset
    + kAboutVersionLineHeight
    + kContentInset
    + kAboutDetailsHeight
    + kContentInset
    + kSubtitleHeight
    + kFooterGap
    + kButtonHeight
    + kOuterInset;
```

The expression evaluates to 428 and must replace the old literal 400.

Set `versionLine.width` to `std::min(132, contentWidth)` while retaining its current left-aligned `x` position. Keep all returned rectangles inside the derived minimum client height.

- [ ] **Step 6: Consume the main header helper in `Application::OnResize`**

Include the new header in `Application.cpp`:

```cpp
#include "MainWindowLayout.h"
```

Replace the three hard-coded title/hint/list offsets with:

```cpp
const MainWindowHeaderLayout header =
    CalculateMainWindowHeaderLayout(innerY);
MoveWindow(m_hTitleLabel,
    innerX, header.titleY, innerWidth, header.titleHeight, TRUE);
MoveWindow(m_hHintLabel,
    innerX, header.hintY, innerWidth, header.hintHeight, TRUE);

const int listTop = header.listTop;
```

Leave button-row, status-card, font, and minimum-window calculations unchanged.

- [ ] **Step 7: Add exact About surface constants and compact chip placement**

Define alongside the existing information-window palette:

```cpp
constexpr COLORREF ABOUT_CARD_SURFACE = RGB(42, 42, 44);
constexpr COLORREF ABOUT_CARD_BORDER = RGB(55, 55, 58);
constexpr COLORREF ABOUT_VERSION_SURFACE = RGB(48, 48, 50);
constexpr COLORREF ABOUT_VERSION_LABEL_TEXT = RGB(160, 160, 165);
constexpr COLORREF ABOUT_VERSION_VALUE_TEXT = RGB(205, 205, 210);
```

In About `WM_SIZE`, fit the two controls inside the 132-pixel chip:

```cpp
constexpr int versionInset = 8;
constexpr int versionLabelWidth = 68;
MoveWindow(state->versionLabel,
    layout.versionLine.x + versionInset,
    layout.versionLine.y,
    versionLabelWidth,
    layout.versionLine.height,
    TRUE);
MoveWindow(state->versionValue,
    layout.versionLine.x + versionInset + versionLabelWidth,
    layout.versionLine.y,
    std::max(0, layout.versionLine.width
        - 2 * versionInset - versionLabelWidth),
    layout.versionLine.height,
    TRUE);
```

Keep `supportingFont` on both version controls.

- [ ] **Step 8: Paint matching soft surfaces without new GDI allocations**

In About `WM_PAINT`, draw:

```cpp
UiRenderer::DrawRoundedPanel(
    hdc, versionChip,
    ABOUT_VERSION_SURFACE, ABOUT_VERSION_SURFACE, 7);
UiRenderer::DrawRoundedPanel(
    hdc, detailsPanel,
    ABOUT_CARD_SURFACE, ABOUT_CARD_BORDER);
SetDCBrushColor(hdc, ABOUT_CARD_BORDER);
FillRect(hdc, &divider,
    static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
```

Update `WM_CTLCOLORSTATIC` so version controls return `ABOUT_VERSION_SURFACE`, About labels/values return `ABOUT_CARD_SURFACE`, and text colors match the constants. Update the About `editBrush` creation and `WM_CTLCOLOREDIT` background to `ABOUT_CARD_SURFACE` so the read-only directory does not paint a rectangular mismatch.

Do not modify any Logs painting or color branch.

- [ ] **Step 9: Run focused and full verification**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: focused 1/1 and full 18/18 tests pass, Release build exits 0, and diff check returns no output.

- [ ] **Step 10: Commit the refinement**

```powershell
rtk git add src/app/MainWindowLayout.h src/app/InfoWindowLayout.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "fix: soften about metadata and text metrics"
```

---

### Task 2: Final verification and whole-range review

**Files:**
- Modify only files required by a concrete verification or review defect.

**Interfaces:**
- Consumes: the refinement commit and the earlier Logs/About alignment commits.
- Produces: clean Release evidence and final read-only review verdict.

- [ ] **Step 1: Run clean automated verification**

```powershell
rtk cmake --build build --config Release --clean-first
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: build exit 0, CTest 18/18, and no diff-check output. Use a fresh isolated SDD build directory if the normal executable is locked; never terminate a user-owned process.

- [ ] **Step 2: Attempt native inspection safely**

Inspect About and the main window in the new executable. Confirm compact chip, softer card, complete lower glyph portions, preserved directory copy/update actions, and unchanged Logs. Do not change Windows scaling or bypass singleton protection. Record host capture error `0x80004002` if it recurs.

- [ ] **Step 3: Request final whole-range review**

Provide a read-only reviewer with one review package covering the earlier alignment commits and the refinement commit. Require checks for spec compliance, GDI lifetime, matching child/control brushes, minimum geometry, unchanged Logs, preserved localization/refresh/copy/update behavior, and the new main-header helper.

- [ ] **Step 4: Fix only Critical/Important findings**

Use one scoped fix subagent for the complete finding list, rerun focused/full tests, commit with:

```powershell
rtk git add src/app/MainWindowLayout.h src/app/InfoWindowLayout.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "fix: polish about refinement"
```

Then request one scoped re-review. Do not create an empty verification commit.
