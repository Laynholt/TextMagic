# Content Surface Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Soften MessageWindow content, repair the Application Blacklist hierarchy, round the remaining large content surfaces, and replace the main hint with concise muted guidance.

**Architecture:** Add the blacklist geometry to the existing pure info-window layout module, keep the main hint font as an explicitly owned application resource, and centralize the approved large-surface radius/colors in a small style header. Reuse the existing Win32 rounded-child-region path and the existing GDI+ rounded-panel renderer; do not introduce a general control framework.

**Tech Stack:** C++17, Win32 controls and window procedures, GDI/GDI+, CMake/MSBuild, CTest, INI localization.

## Global Constraints

- Large content radius is exactly `10 px`.
- MessageWindow fill is exactly `RGB(42, 42, 44)` and border is exactly `RGB(55, 55, 58)`.
- The main script list, blacklist list view, and running-application list view retain their existing fill and text colors.
- Blacklist title height is `40 px`; title-to-checkbox gap is `8 px`; checkbox height is `28 px`.
- Blacklist initial outer size is `760 x 560 px`; minimum outer size is `640 x 460 px`.
- Main hint uses Segoe UI regular `-14` and `RGB(170, 170, 175)`.
- Logs and About must remain visually and behaviorally unchanged.
- Do not change selection modes, item heights, ListView extended styles, context menus, scrollbars, sorting, button behavior, or double-click routing.
- Use `rtk` as the prefix for every shell command.
- Use `apply_patch` for source and documentation edits.

---

### Task 1: Repair the Application Blacklist layout

**Files:**
- Modify: `src/app/InfoWindowLayout.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Consumes: existing `InfoRect` and `info_window_layout_detail::NonNegative` conventions.
- Produces: `BlacklistWindowLayout CalculateBlacklistWindowLayout(int clientWidth, int clientHeight)` and the exact blacklist size constants in `info_window_layout_detail`.

- [ ] **Step 1: Add failing blacklist geometry assertions**

Append these checks to `tests/InfoWindowLayoutTests.cpp` before the main-header checks:

```cpp
    Check(info_window_layout_detail::kBlacklistInitialOuterWidth == 760,
          "blacklist initial width remains approved");
    Check(info_window_layout_detail::kBlacklistInitialOuterHeight == 560,
          "blacklist initial height gains vertical room");
    Check(info_window_layout_detail::kBlacklistMinimumOuterWidth == 640,
          "blacklist minimum width remains approved");
    Check(info_window_layout_detail::kBlacklistMinimumOuterHeight == 460,
          "blacklist minimum height gains vertical room");

    const int blacklistMinimumClientHeight =
        info_window_layout_detail::kBlacklistMinimumClientHeight;
    const BlacklistWindowLayout blacklist = CalculateBlacklistWindowLayout(
        640, blacklistMinimumClientHeight);
    Check(blacklist.title.height == 40,
          "blacklist title leaves room for descenders");
    Check(blacklist.title.y + blacklist.title.height + 8
              <= blacklist.fullscreenCheckbox.y,
          "blacklist checkbox follows the title gap");
    Check(blacklist.fullscreenCheckbox.y + blacklist.fullscreenCheckbox.height
              < blacklist.list.y,
          "blacklist list follows the checkbox");
    Check(blacklist.list.y + blacklist.list.height
              + info_window_layout_detail::kBlacklistFooterGap
              <= blacklist.runningButton.y,
          "blacklist list stays above the footer");
    Check(blacklist.runningButton.y == blacklist.closeButton.y,
          "blacklist footer buttons share a baseline");
    Check(IsInside(blacklist.closeButton, 640, blacklistMinimumClientHeight),
          "blacklist close button stays inside the minimum client");
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
```

Expected: compile failure because `BlacklistWindowLayout`, `CalculateBlacklistWindowLayout`, and the blacklist constants do not exist.

- [ ] **Step 3: Add the pure blacklist layout**

Add this public result type beside `LogsWindowLayout` and `AboutWindowLayout`:

```cpp
struct BlacklistWindowLayout {
    InfoRect title;
    InfoRect fullscreenCheckbox;
    InfoRect list;
    InfoRect runningButton;
    InfoRect exeButton;
    InfoRect removeButton;
    InfoRect closeButton;
};
```

Add the exact constants under `info_window_layout_detail`:

```cpp
constexpr int kBlacklistInitialOuterWidth = 760;
constexpr int kBlacklistInitialOuterHeight = 560;
constexpr int kBlacklistMinimumOuterWidth = 640;
constexpr int kBlacklistMinimumOuterHeight = 460;
constexpr int kBlacklistTitleHeight = 40;
constexpr int kBlacklistTitleGap = 8;
constexpr int kBlacklistCheckboxHeight = 28;
constexpr int kBlacklistListGap = 8;
constexpr int kBlacklistListMinimumHeight = 100;
constexpr int kBlacklistFooterGap = 12;
constexpr int kBlacklistButtonHeight = 34;
constexpr int kBlacklistMinimumClientHeight = kOuterInset
    + kBlacklistTitleHeight
    + kBlacklistTitleGap
    + kBlacklistCheckboxHeight
    + kBlacklistListGap
    + kBlacklistListMinimumHeight
    + kBlacklistFooterGap
    + kBlacklistButtonHeight
    + kOuterInset;
```

Implement `CalculateBlacklistWindowLayout`. Preserve current button widths and gaps:

```cpp
inline BlacklistWindowLayout CalculateBlacklistWindowLayout(
    int clientWidth,
    int clientHeight
) {
    using namespace info_window_layout_detail;
    const int safeWidth = ClientWidth(clientWidth);
    const int effectiveHeight = std::max(
        NonNegative(clientHeight), kBlacklistMinimumClientHeight);
    const int contentWidth = NonNegative(safeWidth - 2 * kOuterInset);

    const InfoRect title{
        kOuterInset, kOuterInset, contentWidth, kBlacklistTitleHeight};
    const InfoRect checkbox{
        kOuterInset,
        title.y + title.height + kBlacklistTitleGap,
        contentWidth,
        kBlacklistCheckboxHeight};
    const int listY = checkbox.y + checkbox.height + kBlacklistListGap;
    const int footerY = effectiveHeight - kOuterInset - kBlacklistButtonHeight;
    const InfoRect list{
        kOuterInset,
        listY,
        contentWidth,
        std::max(kBlacklistListMinimumHeight,
                 footerY - kBlacklistFooterGap - listY)};

    constexpr int runningWidth = 180;
    constexpr int exeWidth = 130;
    constexpr int removeWidth = 110;
    const InfoRect running{kOuterInset, footerY, runningWidth, kBlacklistButtonHeight};
    const InfoRect exe{
        running.x + running.width + kButtonGap,
        footerY, exeWidth, kBlacklistButtonHeight};
    const InfoRect remove{
        exe.x + exe.width + kButtonGap,
        footerY, removeWidth, kBlacklistButtonHeight};
    const InfoRect close{
        std::max(kOuterInset, safeWidth - kOuterInset - 130),
        footerY, std::min(130, contentWidth), kBlacklistButtonHeight};
    return {title, checkbox, list, running, exe, remove, close};
}
```

- [ ] **Step 4: Route blacklist creation, minimum sizing, and resize through the helper**

In `Application.cpp`:

- replace the file-local blacklist outer size literals with the new `info_window_layout_detail` constants;
- set the initial blacklist height to `kBlacklistInitialOuterHeight`;
- keep initial width `kBlacklistInitialOuterWidth`;
- in `WM_GETMINMAXINFO`, use `kBlacklistMinimumOuterWidth` and `kBlacklistMinimumOuterHeight`;
- replace the blacklist branch in `WM_SIZE` with `CalculateBlacklistWindowLayout(w, h)` and `MoveWindow` calls for every returned rectangle;
- keep the two ListView column widths and all commands unchanged.

The branch must have this shape:

```cpp
const BlacklistWindowLayout layout = CalculateBlacklistWindowLayout(w, h);
MoveWindow(state->titleLabel, layout.title.x, layout.title.y,
    layout.title.width, layout.title.height, TRUE);
MoveWindow(state->fullscreenCheckbox,
    layout.fullscreenCheckbox.x, layout.fullscreenCheckbox.y,
    layout.fullscreenCheckbox.width, layout.fullscreenCheckbox.height, TRUE);
MoveWindow(state->blacklistList, layout.list.x, layout.list.y,
    layout.list.width, layout.list.height, TRUE);
MoveWindow(state->runningPickerButton, layout.runningButton.x,
    layout.runningButton.y, layout.runningButton.width,
    layout.runningButton.height, TRUE);
MoveWindow(state->exePickerButton, layout.exeButton.x,
    layout.exeButton.y, layout.exeButton.width, layout.exeButton.height, TRUE);
MoveWindow(state->removeButton, layout.removeButton.x,
    layout.removeButton.y, layout.removeButton.width,
    layout.removeButton.height, TRUE);
MoveWindow(state->closeButton, layout.closeButton.x,
    layout.closeButton.y, layout.closeButton.width,
    layout.closeButton.height, TRUE);
```

- [ ] **Step 5: Run focused and application builds**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
rtk cmake --build build --config Release --target TextMagic
rtk git diff --check
```

Expected: focused test passes, TextMagic builds, and diff check has no output.

- [ ] **Step 6: Commit the blacklist layout**

```powershell
rtk git add src/app/InfoWindowLayout.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "fix: repair blacklist window layout"
```

---

### Task 2: Simplify and mute the main-window guidance

**Files:**
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`
- Modify: `tests/LocalizationTests.cpp`
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Verify: `src/app/MainWindowLayout.h`
- Verify: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Consumes: `hint.label`, `INFO_MUTED_TEXT`, `CalculateMainWindowHeaderLayout`, and the existing application font ownership pattern.
- Produces: owned `HFONT m_hHintFont` using Segoe UI regular `-14`, plus the approved English and Russian `hint.label` strings.

- [ ] **Step 1: Add failing localization checks**

Add this test to `tests/LocalizationTests.cpp`:

```cpp
void TestEmbeddedLanguagesContainConciseMainHint() {
    const std::wstring english = Localization::GetTextByName(L"hint.label", L"en");
    const std::wstring russian = Localization::GetTextByName(L"hint.label", L"ru");

    CHECK(english ==
        L"Scripts work with selected text, the last typed word, or all typed text.\r\n"
        L"Double-click a script to apply it to text from the clipboard.");
    CHECK(russian ==
        L"\u0421\u043a\u0440\u0438\u043f\u0442\u044b \u0440\u0430\u0431\u043e\u0442\u0430\u044e\u0442 \u0441 \u0432\u044b\u0434\u0435\u043b\u0435\u043d\u043d\u044b\u043c \u0442\u0435\u043a\u0441\u0442\u043e\u043c, "
        L"\u043f\u043e\u0441\u043b\u0435\u0434\u043d\u0438\u043c \u0432\u0432\u0435\u0434\u0451\u043d\u043d\u044b\u043c \u0441\u043b\u043e\u0432\u043e\u043c \u0438\u043b\u0438 \u0432\u0441\u0435\u043c \u0432\u0432\u0435\u0434\u0451\u043d\u043d\u044b\u043c \u0442\u0435\u043a\u0441\u0442\u043e\u043c.\r\n"
        L"\u0414\u0432\u043e\u0439\u043d\u043e\u0439 \u0449\u0435\u043b\u0447\u043e\u043a \u043f\u0440\u0438\u043c\u0435\u043d\u044f\u0435\u0442 \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u0439 \u0441\u043a\u0440\u0438\u043f\u0442 \u043a \u0442\u0435\u043a\u0441\u0442\u0443 \u0438\u0437 \u0431\u0443\u0444\u0435\u0440\u0430 \u043e\u0431\u043c\u0435\u043d\u0430.");
    CHECK(english.find(L"Global text scripts") == std::wstring::npos);
    CHECK(russian.find(
        L"\u0413\u043b\u043e\u0431\u0430\u043b\u044c\u043d\u044b\u0435 \u0441\u043a\u0440\u0438\u043f\u0442\u044b")
        == std::wstring::npos);
}
```

Call `TestEmbeddedLanguagesContainConciseMainHint()` from `main()` before the concurrency test.

- [ ] **Step 2: Run localization tests and verify RED**

```powershell
rtk cmake --build build --config Release --target TextMagicLocalizationTests
rtk ctest --test-dir build -C Release -R TextMagicLocalizationTests --output-on-failure
```

Expected: the exact-string checks fail because both embedded hints still contain the old introduction and management note.

- [ ] **Step 3: Replace the two localized hint values**

Set `hint.label` in `lang/en.ini` to:

```ini
hint.label=Scripts work with selected text, the last typed word, or all typed text.\r\nDouble-click a script to apply it to text from the clipboard.
```

Set `hint.label` in `lang/ru.ini` to:

```ini
hint.label=Скрипты работают с выделенным текстом, последним введённым словом или всем введённым текстом.\r\nДвойной щелчок применяет выбранный скрипт к тексту из буфера обмена.
```

- [ ] **Step 4: Add and use the owned supporting font**

In `Application.h`, add this field next to the other owned fonts:

```cpp
HFONT m_hHintFont = nullptr;
```

In `Application::CreateControls`, create it with the exact approved metrics and apply it only to `m_hHintLabel`:

```cpp
m_hHintFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL,
    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

SendMessageW(m_hHintLabel, WM_SETFONT,
    reinterpret_cast<WPARAM>(m_hHintFont), TRUE);
```

Remove the old assignment of `m_hFont` to `m_hHintLabel`. In `Shutdown`, delete `m_hHintFont` and set it to `nullptr` using the same pattern as `m_hTitleFont` and `m_hMonoFont`.

- [ ] **Step 5: Apply the muted hint color without changing other static controls**

In the main window `WM_CTLCOLORSTATIC` branch, insert this control-specific case before the default text color:

```cpp
if (control == m_hHintLabel) {
    SetTextColor(hdc, INFO_MUTED_TEXT);
    return reinterpret_cast<INT_PTR>(m_hCardBrush);
}
```

Do not change `m_hTitleLabel`, `m_hStatusLabel`, buttons, or list text colors.

- [ ] **Step 6: Run focused tests and the application build**

```powershell
rtk cmake --build build --config Release --target TextMagicLocalizationTests TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R "TextMagic(Localization|InfoWindowLayout)Tests" --output-on-failure
rtk cmake --build build --config Release --target TextMagic
rtk git diff --check
```

Expected: both focused tests pass, main-header assertions remain unchanged, TextMagic builds, and diff check has no output.

- [ ] **Step 7: Commit the guidance change**

```powershell
rtk git add lang/en.ini lang/ru.ini tests/LocalizationTests.cpp src/app/Application.h src/app/Application.cpp
rtk git commit -m "style: simplify main window guidance"
```

---

### Task 3: Round shared content surfaces and soften MessageWindow

**Files:**
- Create: `src/app/ContentSurfaceStyle.h`
- Modify: `src/ui/UiRenderer.h`
- Modify: `src/ui/UiRenderer.cpp`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Consumes: `ApplyRoundedChildRegion(HWND, int, int, int)`, `UiRenderer::DrawRoundedPanel`, blacklist layout from Task 1, and main-window geometry already in place.
- Produces: `content_surface_style` constants and `UiRenderer::DrawRoundedControlFrame(HWND, HWND, int, COLORREF, COLORREF, int)`.

- [ ] **Step 1: Add failing style-contract assertions**

Add this include to `tests/InfoWindowLayoutTests.cpp`:

```cpp
#include "../src/app/ContentSurfaceStyle.h"
```

Add these checks before returning from `main()`:

```cpp
    Check(content_surface_style::kCornerRadius == 10,
          "large content surfaces use the Logs radius");
    Check(content_surface_style::kMessageFill == RGB(42, 42, 44),
          "message surface uses the approved soft fill");
    Check(content_surface_style::kMessageBorder == RGB(55, 55, 58),
          "message surface uses the approved soft border");
    Check(content_surface_style::kListFill == RGB(37, 37, 37),
          "existing list fill remains unchanged");
```

- [ ] **Step 2: Run the focused layout target and verify RED**

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compile failure because `ContentSurfaceStyle.h` does not exist.

- [ ] **Step 3: Create the exact style contract**

Create `src/app/ContentSurfaceStyle.h`:

```cpp
#pragma once

#include <windows.h>

namespace content_surface_style {
constexpr int kCornerRadius = 10;
constexpr COLORREF kListFill = RGB(37, 37, 37);
constexpr COLORREF kListBorder = RGB(62, 62, 62);
constexpr COLORREF kMessageFill = RGB(42, 42, 44);
constexpr COLORREF kMessageBorder = RGB(55, 55, 58);
}
```

- [ ] **Step 4: Replace the rectangular control-frame renderer**

In `src/ui/UiRenderer.h`, replace `DrawEditBorder` with:

```cpp
static void DrawRoundedControlFrame(
    HWND parentWindow,
    HWND control,
    int padding,
    COLORREF background,
    COLORREF border,
    int radius = 10);
```

In `src/ui/UiRenderer.cpp`, replace the line-based `DrawEditBorder` implementation. Keep its existing control-to-parent rectangle conversion, inflate by non-negative padding, acquire the parent DC, and delegate painting:

```cpp
void UiRenderer::DrawRoundedControlFrame(
    HWND parentWindow,
    HWND control,
    int padding,
    COLORREF background,
    COLORREF border,
    int radius
) {
    if (!control || !parentWindow) {
        return;
    }
    RECT rect = {};
    GetWindowRect(control, &rect);
    ScreenToClient(parentWindow, reinterpret_cast<LPPOINT>(&rect.left));
    ScreenToClient(parentWindow, reinterpret_cast<LPPOINT>(&rect.right));
    InflateRect(&rect, std::max(0, padding), std::max(0, padding));

    HDC hdc = GetDC(parentWindow);
    if (!hdc) {
        return;
    }
    DrawRoundedPanel(hdc, rect, background, border, radius);
    ReleaseDC(parentWindow, hdc);
}
```

- [ ] **Step 5: Round and frame the main script list**

In `Application::OnResize`, retain the exact list rectangle values, then call `ApplyRoundedChildRegion` using the actual child width/height and `content_surface_style::kCornerRadius`.

In `Application::OnPaint`, replace `DrawEditBorder` with:

```cpp
UiRenderer::DrawRoundedControlFrame(
    m_hWnd,
    m_hScriptList,
    LIST_CONTENT_PADDING,
    content_surface_style::kListFill,
    content_surface_style::kListBorder,
    content_surface_style::kCornerRadius);
```

Do not change list styles, item height, selection, context menu, double-click, or scrollbar code.

- [ ] **Step 6: Round and frame blacklist and running-application tables**

After moving `state->blacklistList` in the blacklist `WM_SIZE` branch, call:

```cpp
ApplyRoundedChildRegion(
    state->blacklistList,
    layout.list.width,
    layout.list.height,
    content_surface_style::kCornerRadius);
```

In the blacklist `WM_PAINT` branch, draw a rounded panel at `layout.list` with `kListFill`, `kListBorder`, and `kCornerRadius` before `EndPaint`.

In `MessageWindowProc::WM_SIZE`, apply the same child region to `state->textControl` after `MoveWindow`, including the running-application ListView. Keep all ListView styles, columns, sorting, multi-selection, and scrollbar calls unchanged.

- [ ] **Step 7: Soften only the generic MessageWindow text surface**

At both `MessageWindowState` construction sites, create `state->editBrush` from `content_surface_style::kMessageFill`.

For generic MessageWindow list boxes:

- set `WM_CTLCOLORLISTBOX` background to `kMessageFill`;
- return `state->editBrush` instead of the application-wide list brush;
- use `kMessageFill` in `WM_CTLCOLOREDIT` if the edit path is active;
- draw the rounded frame with `kMessageFill` and `kMessageBorder`.

For the running-application ListView:

- retain `kListFill` and the current text color;
- draw its frame with `kListFill` and `kListBorder`.

Use this branch when painting the MessageWindow frame:

```cpp
const COLORREF fill = state->runningApplicationSelection
    ? content_surface_style::kListFill
    : content_surface_style::kMessageFill;
const COLORREF border = state->runningApplicationSelection
    ? content_surface_style::kListBorder
    : content_surface_style::kMessageBorder;
UiRenderer::DrawRoundedControlFrame(
    hWnd, state->textControl, 0, fill, border,
    content_surface_style::kCornerRadius);
```

Keep the existing `WM_NCDESTROY` brush deletion. Do not change wrapping, copy/context-menu routes, button commands, or scrollbar behavior.

- [ ] **Step 8: Verify the integrated implementation**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
rtk git status --short
```

Expected: Release build succeeds, all 18 current CTest tests plus any newly registered tests pass, diff check has no output, and status lists only the intended Task 3 files.

- [ ] **Step 9: Perform safe interaction and visual checks**

Launch only the freshly built `build/bin/Release/TextMagic.exe`. Verify, when the environment permits:

- main list is rounded and still selects, scrolls, opens its context menu, and executes on double-click;
- blacklist title is fully visible, checkbox/list/footer do not overlap, add/remove actions and scrolling still work;
- running-application list is rounded and still sorts, multi-selects, and scrolls;
- MessageWindow is softer, rounded, wraps text, copies, scrolls, and closes normally;
- Logs and About show no visual or behavioral change.

Do not change Windows scaling, bypass the singleton, or kill unrelated processes. If capture fails with `SetIsBorderRequired ... 0x80004002`, record the limitation and stop coordinate-based UI input when geometry is unavailable.

- [ ] **Step 10: Commit the shared surface polish**

```powershell
rtk git add src/app/ContentSurfaceStyle.h src/ui/UiRenderer.h src/ui/UiRenderer.cpp src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "style: polish shared content surfaces"
```

## Final Review Checklist

- [ ] Review the complete range from the plan base through Task 3 against `docs/superpowers/specs/2026-08-11-content-surface-polish-design.md`.
- [ ] Confirm Logs and About have no changed constants, geometry, control styles, event routes, or localized text.
- [ ] Confirm every new GDI brush/font has one normal teardown path and every failed `SetWindowRgn` deletes only the unowned region.
- [ ] Confirm blacklist and main layout tests cover minimum-size non-overlap and approved header spacing.
- [ ] Confirm the exact English/Russian hint strings and MessageWindow RGB values.
- [ ] Run a fresh Release build, full CTest, `git diff --check`, and `git status --short` on final HEAD before reporting completion.
