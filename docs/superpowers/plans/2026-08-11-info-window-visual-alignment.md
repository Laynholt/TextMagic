# Info Window Visual Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align the Logs and About windows with the TextMagic main-window palette and hierarchy while preserving every existing interaction.

**Architecture:** Keep the native Win32 controls and the existing `InfoWindowProc`. Put deterministic geometry in `InfoWindowLayout.h`, keep painting/font ownership in `Application.cpp`, and verify geometry through the existing standalone layout test target. The work is split into a Logs/shared-typography task and an About-structure task so each visual surface can be reviewed independently.

**Tech Stack:** C++17, Win32/GDI, CMake/MSBuild, CTest, existing `UiRenderer` helpers.

## Global Constraints

- Reuse the main script-list surface color `RGB(37, 37, 37)`.
- Supporting descriptions use smaller regular Segoe UI text in a muted neutral color; do not use italic text.
- Preserve log selection, copying, context menus, append behavior, conditional dark scrollbar, localization, update checking, and dynamic script refresh.
- Preserve DPI-aware minimum sizing and keep every rectangle inside the supported minimum client area.
- Do not change the main-window layout, global button styling, log persistence, script discovery, update logic, or Windows display scaling.
- Work directly on `master`, as explicitly approved by the user for this feature series.

---

## File map

- `src/app/InfoWindowLayout.h`: deterministic Logs and About rectangles and minimum-client invariants.
- `src/app/Application.cpp`: `InfoWindowState`, control creation, font ownership, localization refresh, resize placement, painting, and control colors.
- `tests/InfoWindowLayoutTests.cpp`: native-free geometry regression coverage.
- `lang/ru.ini`, `lang/en.ini`: retained About/Logs strings; modify only if the compact version line requires punctuation cleanup.
- `CMakeLists.txt`: no target changes expected; existing `TextMagicInfoWindowLayoutTests` remains the focused test.

---

### Task 1: Align shared typography and the Logs surface

**Files:**
- Modify: `src/app/InfoWindowLayout.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Consumes: `Application::m_hTitleFont`, `Application::m_hFont`, `Application::m_hListBrush`, `CalculateLogsWindowLayout(int, int)`.
- Produces: `InfoWindowState::supportingFont`, a 32-pixel title rectangle, an 18-pixel supporting-text rectangle, and Logs painting based on `RGB(37, 37, 37)`.

- [ ] **Step 1: Add failing Logs hierarchy assertions**

Add these assertions after the first `CalculateLogsWindowLayout(900, 600)` call:

```cpp
Check(logs.title.height == 32,
      "logs title reserves the main-heading height");
Check(logs.subtitle.height == 18,
      "logs subtitle uses the compact supporting height");
Check(logs.subtitle.y > logs.title.y + logs.title.height,
      "logs subtitle follows the enlarged title");
```

Keep the existing content/footer and minimum-height assertions.

- [ ] **Step 2: Run the focused test and confirm the red state**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
```

Expected: the focused test fails because the current title and subtitle heights are 28 and 20.

- [ ] **Step 3: Update the deterministic Logs metrics**

In `info_window_layout_detail`, change the shared header metrics and let `kLogsMinimumClientHeight` continue deriving from them:

```cpp
constexpr int kTitleHeight = 32;
constexpr int kSubtitleHeight = 18;
```

Do not hard-code a replacement minimum client height; retain the existing expression so the footer invariant follows the metrics automatically.

- [ ] **Step 4: Add owned supporting typography to `InfoWindowState`**

Add:

```cpp
HFONT supportingFont = nullptr;
```

During `WM_CREATE`, create it once per information window:

```cpp
state->supportingFont = CreateFontW(
    -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
if (!state->supportingFont) {
    return -1;
}
```

Apply `m_hTitleFont` to `titleLabel`, `supportingFont` to `subtitleLabel`, and leave the monospaced log rows on `m_hMonoFont`.

In `WM_NCDESTROY`, delete the owned font before deleting `state`:

```cpp
if (state->supportingFont) {
    DeleteObject(state->supportingFont);
}
```

- [ ] **Step 5: Reuse the main-list palette throughout Logs painting**

Define local file-scope constants near the existing info-window dimensions:

```cpp
constexpr COLORREF INFO_LIST_SURFACE = RGB(37, 37, 37);
constexpr COLORREF INFO_MUTED_TEXT = RGB(170, 170, 175);
constexpr COLORREF INFO_PANEL_BORDER = RGB(52, 52, 56);
```

Replace the Logs-only `RGB(24, 24, 26)` normal surface in all four paths:

```cpp
// DrawLogListBoxItem: unselected row fill
const COLORREF background = selected ? RGB(58, 58, 58) : INFO_LIST_SURFACE;

// WM_PAINT: rounded Logs panel
UiRenderer::DrawRoundedPanel(
    hdc, content, INFO_LIST_SURFACE, INFO_PANEL_BORDER);

// WM_CTLCOLORSTATIC: emptyLabel
SetTextColor(hdc, RGB(230, 230, 230));
return reinterpret_cast<INT_PTR>(state->owner->m_hListBrush);

// WM_CTLCOLORLISTBOX: logList
SetBkColor(hdc, INFO_LIST_SURFACE);
SetTextColor(hdc, RGB(235, 235, 235));
return reinterpret_cast<INT_PTR>(state->owner->m_hListBrush);
```

For `subtitleLabel`, set `INFO_MUTED_TEXT` and return the outer card brush. Do not alter selected-row color, list item height, rounded child region, scrollbar calculation, or menu handling.

- [ ] **Step 6: Run focused and full verification**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: focused test 1/1 passes, full suite 18/18 passes, and diff check returns no output.

- [ ] **Step 7: Commit the Logs/shared-style deliverable**

```powershell
rtk git add src/app/InfoWindowLayout.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "style: align logs window with main ui"
```

---

### Task 2: Simplify the About hierarchy and information card

**Files:**
- Modify: `src/app/InfoWindowLayout.h`
- Modify: `src/app/Application.cpp`
- Modify only if wording requires it: `lang/ru.ini`
- Modify only if wording requires it: `lang/en.ini`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Consumes: `InfoWindowState::supportingFont`, `INFO_LIST_SURFACE`, `INFO_MUTED_TEXT`, `INFO_PANEL_BORDER`, existing localization keys `about.description`, `about.version_label`, `about.loaded_scripts_label`, `about.scripts_directory_label`, and `about.check_updates_hint`.
- Produces: `AboutWindowLayout` with `versionLine`, `detailsPanel`, `loadedScriptsLabel`, `loadedScriptsValue`, `divider`, `directoryLabel`, and `directoryValue`; one left-aligned About information card; left-anchored update action.

- [ ] **Step 1: Replace the old About layout assertions with the new contract**

Replace the assertions that reference `identityPanel` and `pathValue` with:

```cpp
const AboutWindowLayout about = CalculateAboutWindowLayout(620, 440);
Check(about.versionLine.y > about.description.y + about.description.height,
      "about version follows description");
Check(about.detailsPanel.y > about.versionLine.y + about.versionLine.height,
      "about information card follows version");
Check(about.loadedScriptsLabel.x == about.loadedScriptsValue.x,
      "about loaded scripts remain left aligned");
Check(about.loadedScriptsValue.y > about.loadedScriptsLabel.y,
      "about loaded value follows its label");
Check(about.divider.y > about.loadedScriptsValue.y + about.loadedScriptsValue.height,
      "about divider follows loaded scripts");
Check(about.directoryLabel.x == about.directoryValue.x,
      "about directory remains left aligned");
Check(about.directoryLabel.y > about.divider.y,
      "about directory follows divider");
Check(about.directoryValue.y > about.directoryLabel.y,
      "about directory value follows its label");
Check(about.actionButton.x == info_window_layout_detail::kOuterInset,
      "about update action anchors left");
Check(about.actionButton.y == about.closeButton.y,
      "about buttons share a baseline");
```

At the supported minimum client height, replace the old 300-pixel checks with:

```cpp
const int aboutMinimumClientHeight =
    info_window_layout_detail::kAboutMinimumClientHeight;
const AboutWindowLayout minimumAbout =
    CalculateAboutWindowLayout(620, aboutMinimumClientHeight);
Check(IsInside(minimumAbout.title, 620, aboutMinimumClientHeight),
      "about minimum title stays inside");
Check(IsInside(minimumAbout.description, 620, aboutMinimumClientHeight),
      "about minimum description stays inside");
Check(IsInside(minimumAbout.versionLine, 620, aboutMinimumClientHeight),
      "about minimum version stays inside");
Check(IsInside(minimumAbout.detailsPanel, 620, aboutMinimumClientHeight),
      "about minimum information card stays inside");
Check(IsInside(minimumAbout.loadedScriptsLabel, 620, aboutMinimumClientHeight),
      "about minimum loaded label stays inside");
Check(IsInside(minimumAbout.loadedScriptsValue, 620, aboutMinimumClientHeight),
      "about minimum loaded value stays inside");
Check(IsInside(minimumAbout.divider, 620, aboutMinimumClientHeight),
      "about minimum divider stays inside");
Check(IsInside(minimumAbout.directoryLabel, 620, aboutMinimumClientHeight),
      "about minimum directory label stays inside");
Check(IsInside(minimumAbout.directoryValue, 620, aboutMinimumClientHeight),
      "about minimum directory value stays inside");
Check(IsInside(minimumAbout.hint, 620, aboutMinimumClientHeight),
      "about minimum hint stays inside");
Check(IsInside(minimumAbout.actionButton, 620, aboutMinimumClientHeight),
      "about minimum update action stays inside");
Check(IsInside(minimumAbout.closeButton, 620, aboutMinimumClientHeight),
      "about minimum close button stays inside");
Check(minimumAbout.hint.y + minimumAbout.hint.height
          <= minimumAbout.actionButton.y,
      "about minimum hint stays above footer");
```

- [ ] **Step 2: Run the focused test and confirm the compile-time red state**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compilation fails because the current `AboutWindowLayout` still exposes `identityPanel` and `pathValue` instead of the new fields.

- [ ] **Step 3: Implement the new About layout structure**

Replace `AboutWindowLayout` with:

```cpp
struct AboutWindowLayout {
    InfoRect title;
    InfoRect description;
    InfoRect versionLine;
    InfoRect detailsPanel;
    InfoRect loadedScriptsLabel;
    InfoRect loadedScriptsValue;
    InfoRect divider;
    InfoRect directoryLabel;
    InfoRect directoryValue;
    InfoRect hint;
    InfoRect actionButton;
    InfoRect closeButton;
};
```

Use these deterministic metrics:

```cpp
constexpr int kAboutMinimumClientHeight = 400;
constexpr int kAboutDescriptionHeight = 40;
constexpr int kAboutVersionLineHeight = 20;
constexpr int kAboutDetailsHeight = 160;
constexpr int kAboutRowLabelHeight = 18;
constexpr int kAboutValueHeight = 24;
constexpr int kAboutPathHeight = 40;
constexpr int kAboutDividerHeight = 1;
```

Calculate rectangles in this vertical order:

```text
title -> description -> versionLine -> detailsPanel
detailsPanel inset: loaded label -> loaded value -> divider -> directory label -> directory value
detailsPanel -> hint -> footer
```

Both label/value pairs use the same left inset. Set `actionButton.x` to `kOuterInset`; keep `closeButton` right aligned. Clamp against `kAboutMinimumClientHeight` the same way the Logs layout protects its footer.

- [ ] **Step 4: Simplify About controls and localized refresh**

Remove `productLabel` from `InfoWindowState`, its control creation, font assignment, resize call, color branch, null checks, and refresh call.

Keep `versionLabel` and `versionValue` as two compact left-aligned STATIC controls. Change `versionLabel` from `SS_RIGHT` to `SS_LEFT`, and change `loadedScriptsValue` from `SS_RIGHT` to `SS_LEFT`.

In `WM_SIZE`, split `layout.versionLine` at a fixed 76-pixel label width:

```cpp
constexpr int versionLabelWidth = 76;
MoveWindow(state->versionLabel,
    layout.versionLine.x, layout.versionLine.y,
    versionLabelWidth, layout.versionLine.height, TRUE);
MoveWindow(state->versionValue,
    layout.versionLine.x + versionLabelWidth, layout.versionLine.y,
    std::max(0, layout.versionLine.width - versionLabelWidth),
    layout.versionLine.height, TRUE);
```

Place the loaded-script and directory controls directly in the matching layout rectangles. Preserve `CopyOnlyContextSubclassProc` on `directoryValue` and all `RefreshAboutWindow()` calls.

- [ ] **Step 5: Paint one neutral information card and divider**

Remove identity-panel and version-badge drawing. Draw only `detailsPanel`:

```cpp
UiRenderer::DrawRoundedPanel(
    hdc, detailsPanel, INFO_LIST_SURFACE, INFO_PANEL_BORDER);
```

Paint the divider using the existing stock DC brush without allocating a GDI object:

```cpp
SetDCBrushColor(hdc, INFO_PANEL_BORDER);
FillRect(hdc, &divider, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
```

Return `m_hListBrush` for the loaded-script and directory controls. Use `INFO_MUTED_TEXT` for labels, white/high-contrast text for values, and set the read-only directory edit background to `INFO_LIST_SURFACE`.

- [ ] **Step 6: Anchor About buttons and protect the minimum client size**

Generalize the existing DPI-aware track-height helper:

```cpp
int CalculateMinimumTrackHeight(
    HWND hWnd,
    int minimumClientWidth,
    int minimumClientHeight,
    int fallbackOuterHeight);
```

Use it for Logs with `kLogsMinimumClientHeight` and for About with `kAboutMinimumClientHeight`. Keep `ABOUT_MIN_WIDTH` and `ABOUT_MIN_HEIGHT` as fallback outer floors. This ensures the left action and right close footer always remain below the hint at higher DPI.

- [ ] **Step 7: Run focused and full verification**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: focused test 1/1 passes, full suite 18/18 passes, and diff check returns no output.

- [ ] **Step 8: Commit the About deliverable**

```powershell
rtk git add src/app/InfoWindowLayout.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp lang/ru.ini lang/en.ini
rtk git commit -m "style: simplify about window hierarchy"
```

If language files are unchanged, omit them from `git add`.

---

### Task 3: Final native verification and review

**Files:**
- Modify only files required by a concrete defect found during verification.

**Interfaces:**
- Consumes: the completed Release executable and both task commits.
- Produces: clean automated evidence, native inspection evidence when available, and a final whole-range review verdict.

- [ ] **Step 1: Run a clean Release verification**

Run:

```powershell
rtk cmake --build build --config Release --clean-first
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: build exit code 0, CTest 18/18, and no diff-check output. If `TextMagic.exe` is locked by a user-owned process, configure a fresh isolated build directory under `.superpowers/sdd/` and do not terminate that process.

- [ ] **Step 2: Inspect the Logs window**

Using the newly built executable, verify:

- title is visibly larger and semibold;
- subtitle is smaller, regular, and muted;
- list surface matches the main script list rather than the previous near-black panel;
- rounded corners, selection, custom scrollbar, Copy All, close, context menu, and resize remain correct.

- [ ] **Step 3: Inspect the About window**

Verify:

- larger semibold heading and smaller muted description;
- compact left-aligned version line with no identity card/badge;
- one neutral card containing loaded scripts first and directory second;
- both groups are vertically stacked and left aligned;
- subtle divider is visible;
- update action is left anchored and Close is right anchored;
- directory copying, update check, runtime localization, and dynamic script-count refresh still work.

- [ ] **Step 4: Handle host UI-automation limitations safely**

Do not change Windows display scale, bypass the singleton, or terminate user processes. If Computer Use again returns `SetIsBorderRequired ... 0x80004002` or lacks coordinate geometry, record native capture as blocked and rely on automated checks plus read-only code review.

- [ ] **Step 5: Request final whole-range review**

Generate one review package from the plan commit through the current `HEAD`. Ask a read-only reviewer to verify spec coverage, GDI lifetime, DPI layout, localization refresh, preserved Logs interactions, and absence of Critical/Important regressions. Fix every Critical/Important finding through a focused regression cycle and request one scoped re-review.

- [ ] **Step 6: Commit only verification-driven corrections**

If native inspection or final review requires code changes:

```powershell
rtk git add src/app/InfoWindowLayout.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp lang/ru.ini lang/en.ini
rtk git commit -m "fix: polish aligned info windows"
```

Do not create an empty verification commit.
