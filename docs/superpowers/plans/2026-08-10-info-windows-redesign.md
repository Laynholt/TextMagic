# Info Windows Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the Logs and About dialogs with structured dark panels, row-based log selection and copying, and an on-demand dark scrollbar while preserving current TextMagic actions.

**Architecture:** Keep the Win32 window lifecycle in `Application`, but move deterministic log parsing/copying and geometry calculations into two small header-only modules that can be tested without opening a window. `InfoWindowProc` will create variant-specific child controls and delegate refresh/copy/layout work to focused `Application` helpers; `UiRenderer` will provide one reusable rounded-panel primitive.

**Tech Stack:** C++17, Win32 controls and messages, GDI/GDI+, CMake/CTest, existing TextMagic localization and renderer infrastructure.

## Global Constraints

- Change only the Logs and About variants; do not alter the application blacklist behavior.
- Use the current TextMagic neutral palette and existing owner-drawn button language; do not copy the red close button from the reference.
- Every persisted log line is one logical selectable row.
- The dark vertical scrollbar is visible only when the rows exceed the visible capacity.
- Keep update checking, log clearing, context-menu styling, and dialog closing behavior intact.
- Add every new user-facing string to both `lang/ru.ini` and `lang/en.ini`.
- Preserve complete log text for copying even when a visible row is clipped.

## File map

- Create `src/app/InfoWindowModel.h`: pure log splitting, row joining, and scrollbar visibility functions.
- Create `tests/InfoWindowModelTests.cpp`: behavioral tests for the model functions.
- Create `src/app/InfoWindowLayout.h`: pure geometry types and calculations for Logs and About.
- Create `tests/InfoWindowLayoutTests.cpp`: minimum-size and non-overlap layout tests.
- Modify `CMakeLists.txt`: register both new test executables.
- Modify `src/ui/UiRenderer.h` and `src/ui/UiRenderer.cpp`: add the reusable dark rounded-panel renderer.
- Modify `src/app/Application.h` and `src/app/Application.cpp`: create, lay out, refresh, draw, and interact with the new controls.
- Modify `lang/ru.ini` and `lang/en.ini`: add subtitles, structured About labels, copy-all text, and select-all text.

---

### Task 1: Testable log-row model

**Files:**
- Create: `src/app/InfoWindowModel.h`
- Create: `tests/InfoWindowModelTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `std::vector<std::wstring> SplitLogLines(std::wstring_view text)`
- Produces: `std::wstring JoinLogLines(const std::vector<std::wstring>& lines, const std::vector<size_t>& indices)`
- Produces: `bool ShouldShowVerticalScrollbar(size_t itemCount, int itemHeight, int clientHeight)`
- Consumes: only C++17 standard-library types.

- [ ] **Step 1: Write the failing model tests**

Create `tests/InfoWindowModelTests.cpp` with the repository's existing `Check`-and-`main` style:

```cpp
#include "InfoWindowModel.h"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    Check(SplitLogLines(L"").empty(), "empty log has no rows");
    Check(SplitLogLines(L"one") == std::vector<std::wstring>{L"one"},
          "final line without newline is preserved");
    Check(SplitLogLines(L"one\r\ntwo\r\n") ==
              std::vector<std::wstring>{L"one", L"two"},
          "CRLF is split without a trailing empty row");
    Check(SplitLogLines(L"one\ntwo") ==
              std::vector<std::wstring>{L"one", L"two"},
          "LF input is supported");

    const std::vector<std::wstring> rows{L"zero", L"one", L"two"};
    Check(JoinLogLines(rows, {2, 0, 99, 2}) == L"zero\r\ntwo",
          "selected rows are copied once in visual order");
    Check(JoinLogLines(rows, {}).empty(), "empty selection copies nothing");

    Check(!ShouldShowVerticalScrollbar(4, 24, 96),
          "exactly visible rows do not show the scrollbar");
    Check(ShouldShowVerticalScrollbar(5, 24, 96),
          "overflowing rows show the scrollbar");
    Check(!ShouldShowVerticalScrollbar(5, 0, 96),
          "invalid item height hides the scrollbar");
    return 0;
}
```

- [ ] **Step 2: Register and run the missing test target**

Add the target before implementing the header:

```cmake
add_executable(TextMagicInfoWindowModelTests
    tests/InfoWindowModelTests.cpp
)

target_include_directories(TextMagicInfoWindowModelTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

target_compile_features(TextMagicInfoWindowModelTests PRIVATE cxx_std_17)
add_test(NAME TextMagicInfoWindowModelTests COMMAND TextMagicInfoWindowModelTests)
```

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Release --target TextMagicInfoWindowModelTests
```

Expected: compilation fails because `InfoWindowModel.h` does not exist.

- [ ] **Step 3: Implement the minimal header-only functions**

Create `src/app/InfoWindowModel.h` with inline definitions. `SplitLogLines` must scan for `\n`, remove one preceding `\r`, preserve nonempty content, and omit only the synthetic item after a terminal newline. `JoinLogLines` must sort, deduplicate, discard invalid indices, and join valid rows with `\r\n`. `ShouldShowVerticalScrollbar` must return `itemCount > max(1, clientHeight / itemHeight)` only when both dimensions are positive.

```cpp
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

inline std::vector<std::wstring> SplitLogLines(std::wstring_view text) {
    std::vector<std::wstring> rows;
    size_t start = 0;
    while (start < text.size()) {
        const size_t newline = text.find(L'\n', start);
        const size_t end = newline == std::wstring_view::npos ? text.size() : newline;
        size_t contentEnd = end;
        if (contentEnd > start && text[contentEnd - 1] == L'\r') {
            --contentEnd;
        }
        rows.emplace_back(text.substr(start, contentEnd - start));
        if (newline == std::wstring_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return rows;
}
```

Implement the other two functions to the exact semantics asserted above.

- [ ] **Step 4: Run the focused test**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowModelTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowModelTests --output-on-failure
```

Expected: one test passes, zero failures.

- [ ] **Step 5: Commit the model slice**

```powershell
rtk git add CMakeLists.txt src/app/InfoWindowModel.h tests/InfoWindowModelTests.cpp
rtk git commit -m "feat: add log row model"
```

---

### Task 2: Testable dialog geometry

**Files:**
- Create: `src/app/InfoWindowLayout.h`
- Create: `tests/InfoWindowLayoutTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `struct InfoRect { int x; int y; int width; int height; }`
- Produces: `struct LogsWindowLayout` containing `title`, `subtitle`, `content`, `copyAllButton`, and `closeButton` rectangles.
- Produces: `struct AboutWindowLayout` containing `title`, `description`, `identityPanel`, `detailsPanel`, `pathValue`, `hint`, `actionButton`, and `closeButton` rectangles.
- Produces: `LogsWindowLayout CalculateLogsWindowLayout(int clientWidth, int clientHeight)`
- Produces: `AboutWindowLayout CalculateAboutWindowLayout(int clientWidth, int clientHeight)`

- [ ] **Step 1: Write failing geometry tests**

Create `tests/InfoWindowLayoutTests.cpp`:

```cpp
#include "InfoWindowLayout.h"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool IsInside(const InfoRect& child, int width, int height) {
    return child.x >= 0 && child.y >= 0
        && child.x + child.width <= width
        && child.y + child.height <= height;
}
}

int main() {
    const LogsWindowLayout logs = CalculateLogsWindowLayout(900, 600);
    Check(logs.subtitle.y > logs.title.y + logs.title.height,
          "logs subtitle follows title");
    Check(logs.content.y > logs.subtitle.y + logs.subtitle.height,
          "logs panel follows subtitle");
    Check(logs.copyAllButton.y > logs.content.y + logs.content.height,
          "logs footer follows panel");
    Check(IsInside(logs.closeButton, 900, 600), "logs close button stays inside");

    const AboutWindowLayout about = CalculateAboutWindowLayout(620, 440);
    Check(about.identityPanel.y > about.description.y + about.description.height,
          "about identity follows description");
    Check(about.detailsPanel.y > about.identityPanel.y + about.identityPanel.height,
          "about details follow identity");
    Check(about.pathValue.y >= about.detailsPanel.y,
          "path value belongs to details panel");
    Check(about.actionButton.y == about.closeButton.y,
          "about buttons share a baseline");
    Check(IsInside(about.closeButton, 620, 440), "about close button stays inside");
    return 0;
}
```

- [ ] **Step 2: Register and run the missing target**

Add `TextMagicInfoWindowLayoutTests` to `CMakeLists.txt` with `tests/InfoWindowLayoutTests.cpp`, include `src/app`, require C++17, and register it with `add_test`.

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compilation fails because `InfoWindowLayout.h` does not exist.

- [ ] **Step 3: Implement fixed, clamped geometry**

Create `src/app/InfoWindowLayout.h`. Use these shared dimensions: outer inset `16`, content inset `14`, title height `28`, subtitle height `20`, button height `36`, footer gap `14`, and button gap `10`. Clamp calculated content/panel heights to at least `80` for Logs and `110` for About so resizing at the existing minimum sizes never yields negative rectangles.

The Logs calculation must place the content between the subtitle and footer and right-align `180`-pixel Copy All and `140`-pixel Close buttons. The About calculation must place a `64`-pixel identity panel and a `132`-pixel details panel below a `40`-pixel description, reserve a two-line path value inside the details panel, and right-align `210`-pixel Check Updates and `140`-pixel Close buttons.

- [ ] **Step 4: Run the focused test**

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
rtk ctest --test-dir build -C Release -R TextMagicInfoWindowLayoutTests --output-on-failure
```

Expected: one test passes, zero failures.

- [ ] **Step 5: Commit the layout slice**

```powershell
rtk git add CMakeLists.txt src/app/InfoWindowLayout.h tests/InfoWindowLayoutTests.cpp
rtk git commit -m "feat: define info window layouts"
```

---

### Task 3: Row-based Logs dialog

**Files:**
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Modify: `src/ui/UiRenderer.h`
- Modify: `src/ui/UiRenderer.cpp`
- Modify: `lang/ru.ini`
- Modify: `lang/en.ini`

**Interfaces:**
- Consumes: `SplitLogLines`, `JoinLogLines`, `ShouldShowVerticalScrollbar`, and `CalculateLogsWindowLayout` from Tasks 1 and 2.
- Produces: private `Application` helpers `RefreshLogsWindow()`, `CopySelectedLogRows()`, `CopyAllLogRows()`, `SelectAllLogRows()`, and `UpdateLogScrollbar()`.
- Produces: `UiRenderer::DrawRoundedPanel(HDC hdc, const RECT& rect, COLORREF background, COLORREF border, int radius)`.

- [ ] **Step 1: Add localized strings first**

Add these keys with natural translations:

```ini
logs.subtitle=Select the required rows or copy the complete current log.
logs.copy_all=Copy all
menu.select_all=Select all
```

```ini
logs.subtitle=Выделите нужные строки или скопируйте весь текущий лог.
logs.copy_all=Скопировать всё
menu.select_all=Выделить всё
```

- [ ] **Step 2: Add the reusable inner-panel renderer and build**

Declare and implement:

```cpp
static void DrawRoundedPanel(
    HDC hdc,
    const RECT& rect,
    COLORREF background,
    COLORREF border,
    int radius = 10);
```

Use a GDI+ `GraphicsPath`, `SolidBrush`, and one-pixel `Pen`, following the smoothing and rounded-corner construction already used by `DrawCard`. Do not add shadows to inner panels.

Run:

```powershell
rtk cmake --build build --config Release --target TextMagic
```

Expected: the application target builds successfully.

- [ ] **Step 3: Replace the Logs text control with an extended-selection list box**

Add control IDs for the subtitle, log list, empty-state label, Copy All, and Select All menu command. Extend `InfoWindowState` with:

```cpp
HWND subtitleLabel = nullptr;
HWND logList = nullptr;
HWND emptyLabel = nullptr;
HWND copyAllButton = nullptr;
std::vector<std::wstring> logRows;
int hoveredLogIndex = -1;
```

Create the log list with:

```cpp
WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
    | LBS_EXTENDEDSEL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS
    | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY
```

Apply the mono font, set a 25-pixel item height, call `ApplyDarkScrollBar`, and populate it with `SplitLogLines(LogFile::Read(...))`. Use the empty label only when `logRows.empty()`.

If the list box or another required Logs child control cannot be created, return `-1` from `WM_CREATE` so Win32 tears down the partially created window and the existing `WM_NCDESTROY` cleanup owns all state.

- [ ] **Step 4: Implement selection and copying behavior**

Implement the private helpers declared above. `CopySelectedLogRows` must read selected indices with `LB_GETSELCOUNT`/`LB_GETSELITEMS`, call `JoinLogLines`, and write nonempty text using `ClipboardUtils::WriteText`. `CopyAllLogRows` must pass indices `[0, count)` to the same joiner. `SelectAllLogRows` must send `LB_SETSEL(TRUE, -1)`.

Handle:

- Ctrl+A and Ctrl+C in a list-box subclass.
- The new Select All context-menu command.
- Disabled Copy when no list items are selected.
- Right-click hit testing with `LB_ITEMFROMPOINT`; preserve an already selected row and make an unselected hit the sole selection.
- `WM_MOUSEMOVE`/`TrackMouseEvent` hover tracking that updates `hoveredLogIndex`, invalidates only the old and new rows, and clears the state on `WM_MOUSELEAVE`.
- `Copy all` through `ID_INFO_COPY_ALL`.
- Existing Save As and Clear Logs commands without behavior changes.

Extend `ShowStyledContextMenu` with a `copyEnabled` parameter and pass `MF_GRAYED` for `ID_MENU_CONTEXT_COPY` when the selected-row count is zero. Other callers pass `true` to preserve their current behavior.

- [ ] **Step 5: Implement append, clear, and conditional scrolling**

Change `AppendLog` to append one list-box string and one `logRows` entry. Before adding, calculate whether the last row is visible from `LB_GETTOPINDEX`, item height, client height, and current count. Scroll to the new bottom only when the user was already at the bottom.

`RefreshLogsWindow` must reset and repopulate the list after Clear Logs or reopening. `UpdateLogScrollbar` must call `ShouldShowVerticalScrollbar` and `ShowScrollBar(SB_VERT, ...)` after refresh, append, and resize.

- [ ] **Step 6: Lay out and draw the Logs window**

Use `CalculateLogsWindowLayout` in `WM_SIZE`. Set the initial Logs window to approximately `900x600` and update the minimum to match the geometry's safe minimum. In `WM_PAINT`, draw the outer card as today and draw the darker inner panel at the calculated content rectangle with `RGB(24,24,26)` fill and `RGB(52,52,56)` border.

In `WM_DRAWITEM`, draw log rows with mono text and these states:

- normal: `RGB(24,24,26)` background, `RGB(235,235,235)` text;
- hover/focus: `RGB(36,36,40)` background;
- selected: `RGB(58,58,64)` background, `RGB(255,255,255)` text.

Clip text to the row rectangle with left/right padding; keep the stored string unchanged.

- [ ] **Step 7: Run model tests and build the application**

```powershell
rtk ctest --test-dir build -C Release -R "TextMagicInfoWindow(Model|Layout)Tests" --output-on-failure
rtk cmake --build build --config Release --target TextMagic
```

Expected: both focused tests pass and the application builds.

- [ ] **Step 8: Commit the Logs dialog**

```powershell
rtk git add src/app/Application.h src/app/Application.cpp src/ui/UiRenderer.h src/ui/UiRenderer.cpp lang/ru.ini lang/en.ini
rtk git commit -m "feat: redesign logs window"
```

---

### Task 4: Structured About dialog

**Files:**
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Modify: `lang/ru.ini`
- Modify: `lang/en.ini`

**Interfaces:**
- Consumes: `CalculateAboutWindowLayout` and `UiRenderer::DrawRoundedPanel` from Tasks 2 and 3.
- Produces: private `Application::RefreshAboutWindow()`.
- Removes: the single-body-text dependency from `BuildAboutText`; remove `BuildAboutText` if it has no callers after migration.

- [ ] **Step 1: Add the structured About localization**

Add:

```ini
about.description=TextMagic runs user text scenarios through configurable hotkeys.
about.product=TextMagic
about.version_label=Version
about.loaded_scripts_label=Loaded scripts
about.scripts_directory_label=Scripts directory
```

```ini
about.description=TextMagic запускает пользовательские текстовые сценарии с помощью настраиваемых горячих клавиш.
about.product=TextMagic
about.version_label=Версия
about.loaded_scripts_label=Загружено скриптов
about.scripts_directory_label=Каталог скриптов
```

Keep `about.check_updates_hint` and `info.button.check_updates` as the hint and action text.

- [ ] **Step 2: Create structured About controls**

Extend `InfoWindowState` with handles for description, product, version label/value, loaded-scripts label/value, directory label/value, and update hint. Use read-only multiline `EDIT` only for the directory value so wrapping and the existing copy-only context menu remain available; use transparent `STATIC` controls for other labels and values.

Apply the regular UI font, make the product title visually stronger with the existing title font, and make label text muted through `WM_CTLCOLORSTATIC` routing.

- [ ] **Step 3: Refresh dynamic About data**

Implement `RefreshAboutWindow()` to set:

```cpp
SetWindowTextW(state->versionValue, APP_VERSION);
SetWindowTextW(state->loadedScriptsValue, std::to_wstring(m_scripts.size()).c_str());
SetWindowTextW(state->directoryValue, m_scriptsDirectory.c_str());
```

Call it after creation, whenever an existing About window is activated, after localization changes, and after script reloads while About is open.

The refresh helper must also update the localized description, row labels, update hint, and button text so an open About window changes language immediately. Apply the corresponding localization refresh to the open Logs subtitle, empty-state label, and Copy All button.

- [ ] **Step 4: Lay out and draw the About cards**

Use `CalculateAboutWindowLayout` during `WM_SIZE`. Increase the initial client-space target to approximately `620x440`, preserve the non-maximizable style, and draw:

- identity panel with `RGB(34,34,37)` fill;
- compact version badge within the identity panel with `RGB(56,56,62)` fill;
- details panel with `RGB(24,24,26)` fill;
- one-pixel `RGB(52,52,56)` borders.

Place Check Updates and Close on the shared footer baseline. Ensure the directory edit has no native light border and returns the inner-panel brush from `WM_CTLCOLOREDIT`.

If a required About child control cannot be created, return `-1` from `WM_CREATE` and allow normal window teardown to release the state and brush.

- [ ] **Step 5: Preserve actions and context behavior**

Keep Check Updates routed to `Application::CheckForUpdates`, Close routed to `DestroyWindow`, and directory right-click routed to the copy-only styled context menu. The Copy command for the directory edit continues to call `CopyEditSelectionOrAll`.

- [ ] **Step 6: Build and run all automated tests**

```powershell
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
```

Expected: complete build succeeds and CTest reports zero failed tests.

- [ ] **Step 7: Commit the About dialog**

```powershell
rtk git add src/app/Application.h src/app/Application.cpp lang/ru.ini lang/en.ini
rtk git commit -m "feat: redesign about window"
```

---

### Task 5: Native interaction and visual verification

**Files:**
- Modify only files required by defects found during verification.

**Interfaces:**
- Consumes: the finished Release executable in `build/bin/Release/TextMagic.exe`.
- Produces: verified Logs and About behavior at normal DPI and one scaled-DPI setting.

- [ ] **Step 1: Run fresh automated verification**

```powershell
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: build exit code `0`, zero failed tests, and no whitespace errors.

- [ ] **Step 2: Verify Logs interactions in the running application**

Launch the Release executable and exercise:

- empty state and hidden scrollbar;
- short content and hidden scrollbar;
- overflowing content and dark visible scrollbar;
- single click, Ctrl-click disjoint selection, and Shift range selection;
- Ctrl+A, Ctrl+C, context Copy, Select All, Save As, Clear Logs, and Copy All;
- right-click preservation of an existing multi-selection;
- append while at bottom and append while scrolled upward;
- resize down to the minimum and back up.

Capture a screenshot of the populated Logs window for comparison with the approved reference.

- [ ] **Step 3: Verify About interactions in the running application**

Confirm product description, product name, version badge, loaded-script count, directory, update hint, Check Updates, and Close. Right-click the directory and copy it. Reload scripts while About remains open and confirm the count refreshes.

Capture a screenshot of About and inspect alignment, wrapping, dark panels, and footer spacing.

- [ ] **Step 4: Check scaled DPI**

Use one Windows scale above 100 percent, reopen both dialogs, and verify that text, controls, panels, and buttons remain inside their client areas without clipping.

- [ ] **Step 5: Fix any observed defect through a focused red-green cycle**

For deterministic model or geometry defects, first add a failing assertion to the corresponding test file, run the focused test to confirm the failure, implement the smallest correction, and rerun it. For native painting-only defects, reproduce the defect, make the smallest scoped renderer/layout correction, rebuild, and repeat the exact visual check.

- [ ] **Step 6: Run final verification and commit corrections**

```powershell
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

If verification required corrections:

```powershell
rtk git add CMakeLists.txt src tests lang
rtk git commit -m "fix: polish info window interactions"
```

Expected: clean build, zero failed tests, no diff-check errors, and both dialogs match the approved interaction and visual criteria.
