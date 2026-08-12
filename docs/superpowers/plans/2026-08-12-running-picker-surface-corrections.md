# Running Picker Surface Corrections Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the runtime-visible ListView scrollbars, white frame/corner artifacts, selected-row colors, running-picker heading, and Logs corner presentation while retaining version 1.1.0.

**Architecture:** Keep the native Win32 ListViews and existing themed scrollbar hook. Centralize deterministic frame, row-color, and inset decisions in `ContentSurfaceStyle.h`; use one narrow ListView paint subclass and one shared `NM_CUSTOMDRAW` handler for both tables; expose the existing Logs parent panel by physically insetting only its child controls.

**Tech Stack:** C++17, Win32/Common Controls, DWM/UxTheme, CMake, CTest, existing TextMagic GDI/GDI+ renderer.

## Global Constraints

- Running-picker heading is `Запущенные приложения` / `Running applications`, with no ellipsis; the existing action button text remains unchanged.
- Both table controls use TextMagic's existing themed dark scrollbar path; do not disable their visual theme and do not add standalone scrollbar controls.
- Shared radius is `10 px`; table region inset is `2 px`; Logs visual child inset is `2 px`.
- Table normal fill/text are `RGB(37,37,37)` / `RGB(245,245,245)`; selected fill/text are `RGB(35,105,68)` / white.
- Suppress only native `CDIS_SELECTED | CDIS_HOT` paint flags; preserve `CDIS_FOCUS` and actual `LVIS_SELECTED` state.
- Preserve columns, widths, header painting, sorting, multi-selection, blacklist selection/removal, keyboard routes, scroll behavior, and button commands.
- Preserve Logs row height, owner-draw text, selection, copy, context menu, append behavior, conditional scrollbar, and footer layout.
- Project/runtime/resource version remains `1.1.0` / `1,1,0,0`.
- Every shell command starts with `rtk`; edits use `apply_patch`; strict RED -> GREEN -> REFACTOR.

---

### Task 1: Separate the picker heading and restore themed dark table scrollbars

**Files:**
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`
- Modify: `src/app/Application.cpp`
- Modify: `src/app/ContentSurfaceStyle.h`
- Modify: `tests/LocalizationTests.cpp`
- Modify: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces localization key `application_blacklist.running_title`.
- Produces `content_surface_style::ScrollbarSurface` and `UsesExplorerScrollbarTheme(ScrollbarSurface)`.
- Consumes existing `ApplyDarkScrollBar(HWND, bool)` and the accepted running-picker title layout.

- [ ] **Step 1: Add failing localization and scrollbar-policy tests**

Extend `TestEmbeddedLanguagesContainApplicationBlacklistKeys()`:

```cpp
    CHECK(std::wstring(Localization::GetTextByName(
        L"application_blacklist.running_title", L"en"))
        == L"Running applications");
    CHECK(std::wstring(Localization::GetTextByName(
        L"application_blacklist.running_title", L"ru"))
        == L"\u0417\u0430\u043f\u0443\u0449\u0435\u043d\u043d\u044b\u0435 "
           L"\u043f\u0440\u0438\u043b\u043e\u0436\u0435\u043d\u0438\u044f");
    CHECK(std::wstring(Localization::GetTextByName(
        L"application_blacklist.running", L"en"))
        == L"Running applications...");
    CHECK(std::wstring(Localization::GetTextByName(
        L"application_blacklist.running", L"ru"))
        == L"\u0418\u0437 \u0437\u0430\u043f\u0443\u0449\u0435\u043d\u043d\u044b\u0445...");
```

Add to `tests/InfoWindowLayoutTests.cpp`:

```cpp
    using content_surface_style::ScrollbarSurface;
    Check(content_surface_style::UsesExplorerScrollbarTheme(
              ScrollbarSurface::BlacklistTable),
          "blacklist table uses the existing themed dark scrollbar");
    Check(content_surface_style::UsesExplorerScrollbarTheme(
              ScrollbarSurface::RunningPickerTable),
          "running picker table uses the existing themed dark scrollbar");
    Check(content_surface_style::UsesExplorerScrollbarTheme(
              ScrollbarSurface::LogsList),
          "logs keep the existing themed dark scrollbar");
    Check(content_surface_style::UsesExplorerScrollbarTheme(
              ScrollbarSurface::GenericMessageList),
          "generic message lists keep the existing scrollbar policy");
```

The localization test catches accidental title/button key reuse. The policy test catches the regression that explicitly disabled the themed scrollbar route.

- [ ] **Step 2: Verify RED**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicLocalizationTests TextMagicInfoWindowLayoutTests
```

Expected: compile fails on missing `ScrollbarSurface`/policy symbols, or after adding only the test-facing declarations localization fails because the new title key is absent. Record the exact expected failure before production edits.

- [ ] **Step 3: Add title localization and policy contract**

Add next to `application_blacklist.running`:

```ini
# lang/en.ini
application_blacklist.running_title=Running applications

# lang/ru.ini
application_blacklist.running_title=Запущенные приложения
```

Add in `ContentSurfaceStyle.h`:

```cpp
enum class ScrollbarSurface {
    BlacklistTable,
    RunningPickerTable,
    LogsList,
    GenericMessageList,
};

constexpr bool UsesExplorerScrollbarTheme(ScrollbarSurface) {
    return true;
}
```

- [ ] **Step 4: Route production title and scrollbar calls through the corrected decisions**

In `SelectRunningApplications()` set:

```cpp
state->title = T(L"application_blacklist.running_title");
```

For the blacklist ListView remove `SetWindowTheme(..., L"", L"")` and call:

```cpp
ApplyDarkScrollBar(
    state->blacklistList,
    content_surface_style::UsesExplorerScrollbarTheme(
        content_surface_style::ScrollbarSurface::BlacklistTable));
```

For the running-picker ListView do the same with `RunningPickerTable`. Keep Logs and generic MessageWindow ListBoxes on their existing default calls; their policy tests protect the intended unchanged behavior.

- [ ] **Step 5: Verify GREEN**

```powershell
rtk cmake --build build --config Release --target TextMagicLocalizationTests TextMagicInfoWindowLayoutTests TextMagic
rtk ctest --test-dir build -C Release -R "TextMagic(Localization|InfoWindowLayout)Tests" --output-on-failure
rtk git diff --check
```

Expected: 2/2 focused tests pass, TextMagic builds, diff check clean.

- [ ] **Step 6: Commit**

```powershell
rtk git add lang/en.ini lang/ru.ini src/app/Application.cpp src/app/ContentSurfaceStyle.h tests/LocalizationTests.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "fix: restore running table title and scrollbars"
```

---

### Task 2: Own table frame painting and green selected-row visuals

**Files:**
- Modify: `src/app/ContentSurfaceStyle.h`
- Modify: `src/app/Application.cpp`
- Modify: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces `kTableRegionInset`, `kListText`, `kListSelectedFill`, and `kListSelectedText`.
- Produces `StripListViewFrameStyle(DWORD)`, `StripListViewFrameExStyle(DWORD)`, `ResolveListRowVisual(bool)`, and `ResolveListRowPaint(UINT,bool)`.
- Produces one shared ListView surface subclass and one shared `NM_CUSTOMDRAW` handler used by both tables.

- [ ] **Step 1: Add failing pure-contract tests**

Add to `tests/InfoWindowLayoutTests.cpp`:

```cpp
    constexpr DWORD baseStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP
        | WS_VSCROLL | LVS_REPORT | LVS_SHOWSELALWAYS;
    Check(content_surface_style::StripListViewFrameStyle(baseStyle | WS_BORDER)
              == baseStyle,
          "table style stripping removes only WS_BORDER");
    Check(content_surface_style::StripListViewFrameStyle(baseStyle) == baseStyle,
          "table style stripping is idempotent");

    constexpr DWORD baseExStyle = WS_EX_NOPARENTNOTIFY | WS_EX_CONTROLPARENT;
    Check(content_surface_style::StripListViewFrameExStyle(
              baseExStyle | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)
              == baseExStyle,
          "table ex-style stripping removes only native frame edges");
    Check(content_surface_style::StripListViewFrameExStyle(baseExStyle)
              == baseExStyle,
          "table ex-style stripping is idempotent");

    const auto normalRow = content_surface_style::ResolveListRowVisual(false);
    Check(normalRow.fill == RGB(37, 37, 37)
              && normalRow.text == RGB(245, 245, 245),
          "normal table rows use the approved dark palette");
    const auto selectedRow = content_surface_style::ResolveListRowVisual(true);
    Check(selectedRow.fill == RGB(35, 105, 68)
              && selectedRow.text == RGB(255, 255, 255),
          "selected table rows use the approved green palette");

    constexpr UINT selectedHotFocused = CDIS_SELECTED | CDIS_HOT | CDIS_FOCUS;
    const auto selectedPaint = content_surface_style::ResolveListRowPaint(
        selectedHotFocused, true);
    Check((selectedPaint.itemState & (CDIS_SELECTED | CDIS_HOT)) == 0,
          "custom draw suppresses native selected and hot overlays");
    Check((selectedPaint.itemState & CDIS_FOCUS) != 0,
          "custom draw preserves keyboard focus indication");
    Check(content_surface_style::kTableRegionInset == 2,
          "tables expose a two-pixel rounded frame");
```

- [ ] **Step 2: Verify RED**

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compilation fails on the missing tokens/types/functions.

- [ ] **Step 3: Add exact pure style and row-paint contracts**

In `ContentSurfaceStyle.h`, add `<commctrl.h>` and:

```cpp
constexpr int kTableRegionInset = 2;
constexpr COLORREF kListText = RGB(245, 245, 245);
constexpr COLORREF kListSelectedFill = RGB(35, 105, 68);
constexpr COLORREF kListSelectedText = RGB(255, 255, 255);

struct ListRowVisual {
    COLORREF fill;
    COLORREF text;
};

struct ListRowPaint {
    ListRowVisual visual;
    UINT itemState;
};

constexpr DWORD StripListViewFrameStyle(DWORD style) {
    return style & ~static_cast<DWORD>(WS_BORDER);
}

constexpr DWORD StripListViewFrameExStyle(DWORD exStyle) {
    return exStyle & ~static_cast<DWORD>(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
}

constexpr ListRowVisual ResolveListRowVisual(bool selected) {
    return selected
        ? ListRowVisual{kListSelectedFill, kListSelectedText}
        : ListRowVisual{kListFill, kListText};
}

constexpr ListRowPaint ResolveListRowPaint(UINT itemState, bool selected) {
    return {
        ResolveListRowVisual(selected),
        itemState & ~static_cast<UINT>(CDIS_SELECTED | CDIS_HOT),
    };
}
```

- [ ] **Step 4: Add shared production helpers**

In the existing anonymous namespace in `Application.cpp`, add:

```cpp
void StripNativeListViewFrame(HWND listView) {
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(listView, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(listView, GWL_EXSTYLE));
    SetWindowLongPtrW(
        listView, GWL_STYLE,
        static_cast<LONG_PTR>(content_surface_style::StripListViewFrameStyle(style)));
    SetWindowLongPtrW(
        listView, GWL_EXSTYLE,
        static_cast<LONG_PTR>(content_surface_style::StripListViewFrameExStyle(exStyle)));
    SetWindowPos(
        listView, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
            | SWP_FRAMECHANGED);
}
```

Add `PaintDarkListViewSurfaceFrame(HWND)` using `GetWindowDC`, stock `DC_PEN` and `NULL_BRUSH`, `SetDCPenColor(kListBorder)`, and `RoundRect` at the table inset with diameter `2 * kCornerRadius`. It must restore selected stock objects and release the DC; create no owned GDI objects.

Add `DarkListViewSurfaceSubclassProc`:

```cpp
LRESULT CALLBACK DarkListViewSurfaceSubclassProc(
    HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR
) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, DarkListViewSurfaceSubclassProc, subclassId);
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }
    const LRESULT result = DefSubclassProc(hWnd, message, wParam, lParam);
    if (message == WM_NCPAINT || message == WM_PAINT) {
        PaintDarkListViewSurfaceFrame(hWnd);
    }
    if (message == WM_THEMECHANGED || message == WM_SIZE) {
        RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
    }
    return result;
}
```

Add a shared `HandleListViewCustomDraw(HWND,NMHDR*,LRESULT&)` that:

- returns `false` unless `header->hwndFrom == listView && header->code == NM_CUSTOMDRAW`;
- returns `CDRF_NOTIFYITEMDRAW` for `CDDS_PREPAINT`;
- for `CDDS_ITEMPREPAINT`, queries `ListView_GetItemState(row, LVIS_SELECTED)`, resolves `ListRowPaint`, assigns `clrText`, `clrTextBk`, and the filtered `nmcd.uItemState`, then returns `CDRF_DODEFAULT`;
- returns `false` for other stages.

- [ ] **Step 5: Install the frame and custom-draw paths for both tables**

After each ListView is created and colored:

```cpp
StripNativeListViewFrame(listView);
SetWindowSubclass(listView, DarkListViewSurfaceSubclassProc, 1, 0);
```

Subclass-install failure is cosmetic and must not abort window creation.

In both table `WM_SIZE` paths pass `kTableRegionInset` to `ApplyRoundedChildRegion`.

In `InfoWindowProc::WM_NOTIFY`, call `HandleListViewCustomDraw` before the existing blacklist item/key handlers and remove the old inline gray custom-draw branch.

In `MessageWindowProc::WM_NOTIFY`, call the same helper before the existing `LVN_COLUMNCLICK` branch. Do not change sort or selected-path extraction logic.

- [ ] **Step 6: Verify GREEN**

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests TextMagicRunningApplicationTests TextMagic
rtk ctest --test-dir build -C Release -R "TextMagic(InfoWindowLayout|RunningApplication)Tests" --output-on-failure
rtk git diff --check
```

Expected: 2/2 focused tests pass, TextMagic builds, diff check clean.

- [ ] **Step 7: Commit**

```powershell
rtk git add src/app/ContentSurfaceStyle.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "fix: own table frames and selection colors"
```

---

### Task 3: Expose the Logs rounded panel and re-verify Release 1.1.0

**Files:**
- Modify: `src/app/ContentSurfaceStyle.h`
- Modify: `src/app/Application.cpp`
- Modify: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces `SurfaceRect` and `InsetSurfaceRect(int width, int height, int inset)`.
- Consumes accepted `LogsWindowLayout`, `kLogsRegionInset`, `kListBorder`, and existing `ApplyRoundedChildRegion`.

- [ ] **Step 1: Add failing inset-geometry tests**

Add:

```cpp
    const auto insetSurface = content_surface_style::InsetSurfaceRect(100, 80, 2);
    Check(insetSurface.x == 2 && insetSurface.y == 2
              && insetSurface.width == 96 && insetSurface.height == 76,
          "logs child inset exposes the rounded parent frame");
    const auto tinySurface = content_surface_style::InsetSurfaceRect(3, 2, 2);
    Check(tinySurface.width == 0 && tinySurface.height == 0,
          "tiny inset surfaces clamp dimensions to zero");
```

- [ ] **Step 2: Verify RED**

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests
```

Expected: compile fails because `SurfaceRect`/`InsetSurfaceRect` do not exist.

- [ ] **Step 3: Add the pure clamped inset helper**

In `ContentSurfaceStyle.h`:

```cpp
struct SurfaceRect {
    int x;
    int y;
    int width;
    int height;
};

constexpr SurfaceRect InsetSurfaceRect(int width, int height, int inset) {
    const int safeWidth = width > 0 ? width : 0;
    const int safeHeight = height > 0 ? height : 0;
    const int safeInset = inset > 0 ? inset : 0;
    return {
        safeInset,
        safeInset,
        safeWidth > 2 * safeInset ? safeWidth - 2 * safeInset : 0,
        safeHeight > 2 * safeInset ? safeHeight - 2 * safeInset : 0,
    };
}
```

- [ ] **Step 4: Inset only the Logs child controls**

In the Logs `WM_SIZE` branch:

```cpp
const auto child = content_surface_style::InsetSurfaceRect(
    layout.content.width,
    layout.content.height,
    content_surface_style::kLogsRegionInset);
MoveWindow(
    state->logList,
    layout.content.x + child.x,
    layout.content.y + child.y,
    child.width,
    child.height,
    TRUE);
MoveWindow(
    state->emptyLabel,
    layout.content.x + child.x,
    layout.content.y + child.y,
    child.width,
    child.height,
    TRUE);
```

Apply both child regions using `child.width` and `child.height`. Keep `UpdateLogScrollbar()` after the moves.

In the Logs parent paint branch, replace the one-off low-contrast border with:

```cpp
UiRenderer::DrawRoundedPanel(
    hdc, content, INFO_LIST_SURFACE, content_surface_style::kListBorder);
```

Do not change the Logs scrollbar theme call.

- [ ] **Step 5: Verify focused and full GREEN**

```powershell
rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests TextMagic
rtk ctest --test-dir build -C Release -R "TextMagicInfoWindowLayoutTests" --output-on-failure
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
rtk git status --short
```

Expected: focused test 1/1, full Release suite 19/19, diff check clean; before commit only the three owned files are modified.

- [ ] **Step 6: Attempt safe runtime inspection**

Launch only the exact worktree `build/bin/Release/TextMagic.exe`. Verify the corrected title, dark table scrollbars, green selection, absence of white table frame/corners, and visible Logs arc. Also exercise sorting, multi-selection, add/cancel, blacklist remove, Logs selection/copy, and conditional scrolling.

If computer-use capture fails with `SetIsBorderRequired ... 0x80004002`, close only the exact worktree process, record the gap, and do not claim visual success. The user may perform the final visual inspection from the rebuilt executable.

- [ ] **Step 7: Commit**

```powershell
rtk git add src/app/ContentSurfaceStyle.h src/app/Application.cpp tests/InfoWindowLayoutTests.cpp
rtk git commit -m "fix: expose logs rounded surface"
```

---

## Final Review and Integration

- [ ] Generate a whole-branch review package from `28f9720` through corrective HEAD, including the original commits and corrective addendum.
- [ ] Point the reviewer at the ledger line recording the rejected first surface attempt and the report-only Task 3 hash typo.
- [ ] Require explicit review of theme-hook compatibility, subclass lifetime/paint recursion, GDI ownership, custom-draw state preservation, localization separation, Logs geometry, and version 1.1.0.
- [ ] If findings exist, dispatch one fix agent for the complete finding set and one scoped re-review.
- [ ] Run a final fresh Release build, full 19-test CTest, `git diff --check`, and clean status.
- [ ] Integrate the reviewed branch to `master`, remove the isolated worktree, and rebuild `F:\Data\Code\C++\TextMagic\build\bin\Release\TextMagic.exe` on `master`.
