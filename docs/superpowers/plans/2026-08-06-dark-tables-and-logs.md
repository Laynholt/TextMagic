# Dark Tables and Logs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the dark logs UI, style ListView headers consistently, and turn the running-application picker into a sortable three-column table.

**Architecture:** Keep all Win32 dialog/control wiring in the existing `Application.cpp`, reuse the current dark-scrollbar hook and ListView palette, and extend the existing header-only `RunningApplication` model with one case-insensitive comparator. Use native report ListView behavior for selection, resizing, column reordering, and item sorting; add only one shared header subclass for deterministic dark painting.

**Tech Stack:** C++17, Win32 Common Controls (`WC_LISTVIEWW`, `WC_HEADERW`, `SetWindowSubclass`), RichEdit 4.1, GDI, CMake/CTest.

## Global Constraints

- No new dependency or persistent UI configuration is required.
- Keep RichEdit append behavior so the earlier overlapping-line repaint bug does not return.
- Column widths, order, and sort state exist only while the running-application picker is open.
- Existing blacklist persistence, deduplication, multi-selection, and Add selected behavior remain unchanged.
- Dark-mode API failure must not prevent any window from opening.
- Run the smallest named check after each change, then run the complete Release suite before completion.

---

### Task 1: Add a tested running-application comparator

**Files:**

- Modify: `src/app/RunningApplication.h`
- Modify: `tests/RunningApplicationTests.cpp`

**Interfaces:**

- Consumes: existing `RunningApplication { executableName, windowTitle, path }`.
- Produces: `enum class RunningApplicationColumn` and `CompareRunningApplications(const RunningApplication&, const RunningApplication&, RunningApplicationColumn) noexcept` returning `-1`, `0`, or `1` using case-insensitive ordinal comparison.

- [ ] **Step 1: Write the failing comparator checks**

Append these assertions to `tests/RunningApplicationTests.cpp` before `return 0;`:

```cpp
    const RunningApplication alpha = {
        L"Alpha.exe", L"Zulu window", L"C:\\Apps\\Alpha.exe"
    };
    const RunningApplication beta = {
        L"beta.exe", L"alpha window", L"D:\\Tools\\beta.exe"
    };

    Expect(CompareRunningApplications(
        alpha, beta, RunningApplicationColumn::ExecutableName
    ) < 0, "application-name comparison must be case-insensitive ascending");
    Expect(CompareRunningApplications(
        alpha, beta, RunningApplicationColumn::WindowTitle
    ) > 0, "window-title comparison must use the selected column");
    Expect(CompareRunningApplications(
        alpha, beta, RunningApplicationColumn::Path
    ) < 0, "path comparison must use the selected column");
    Expect(CompareRunningApplications(
        beta, alpha, RunningApplicationColumn::ExecutableName
    ) > 0, "reversed arguments must support descending sorting");
    Expect(CompareRunningApplications(
        alpha, { L"ALPHA.EXE", L"ignored", L"ignored" },
        RunningApplicationColumn::ExecutableName
    ) == 0, "case variants must compare equal");
```

- [ ] **Step 2: Run the test to verify it fails**

```powershell
cmake --build build --config Release --target TextMagicRunningApplicationTests
```

Expected: compilation fails because `RunningApplicationColumn` and `CompareRunningApplications` do not exist.

- [ ] **Step 3: Implement the minimal comparator**

Add this API to `src/app/RunningApplication.h` after `RunningApplication`:

```cpp
enum class RunningApplicationColumn {
    ExecutableName = 0,
    WindowTitle = 1,
    Path = 2
};

inline int CompareRunningApplications(
    const RunningApplication& left,
    const RunningApplication& right,
    RunningApplicationColumn column
) noexcept {
    const std::wstring* leftValue = &left.executableName;
    const std::wstring* rightValue = &right.executableName;
    if (column == RunningApplicationColumn::WindowTitle) {
        leftValue = &left.windowTitle;
        rightValue = &right.windowTitle;
    } else if (column == RunningApplicationColumn::Path) {
        leftValue = &left.path;
        rightValue = &right.path;
    }

    const int result = CompareStringOrdinal(
        leftValue->c_str(), -1, rightValue->c_str(), -1, TRUE
    );
    if (result == CSTR_LESS_THAN) {
        return -1;
    }
    if (result == CSTR_GREATER_THAN) {
        return 1;
    }
    return 0;
}
```

- [ ] **Step 4: Run the focused test**

```powershell
cmake --build build --config Release --target TextMagicRunningApplicationTests
ctest --test-dir build -C Release -R TextMagicRunningApplicationTests --output-on-failure
```

Expected: `TextMagicRunningApplicationTests` passes.

- [ ] **Step 5: Commit the comparator**

```powershell
git add src/app/RunningApplication.h tests/RunningApplicationTests.cpp
git commit -m "test: cover running application sorting"
```

---

### Task 2: Add one reusable dark ListView header

**Files:**

- Modify: `src/app/Application.cpp:150-220` (anonymous-namespace drawing helpers)
- Modify: `src/app/Application.cpp:4635-4660` (blacklist ListView creation)

**Interfaces:**

- Consumes: a report-mode ListView whose columns have already been inserted.
- Produces: `ApplyDarkListViewHeader(HWND listView)` and its private `DarkHeaderSubclassProc` painter.

- [ ] **Step 1: Record the failing UI baseline**

Run the current Release executable, open `Дополнительно -> Чёрный список приложений`, and confirm the column header is the default light Windows header while the body is dark. This is the pre-fix red check represented by the supplied screenshot.

- [ ] **Step 2: Add the shared header painter**

Add these exact declarations near the existing list drawing helpers in `Application.cpp`:

```cpp
constexpr UINT_PTR DARK_HEADER_SUBCLASS_ID = 1;

void PaintDarkListViewHeader(HWND header, HDC hdc);

LRESULT CALLBACK DarkHeaderSubclassProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData
);

void ApplyDarkListViewHeader(HWND listView);
```

Implement `PaintDarkListViewHeader` with these concrete rules:

```cpp
// Whole header: RGB(45,45,45).
// Hot cell: RGB(58,58,58); pressed hot cell: RGB(68,68,68).
// Label: RGB(245,245,245), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS.
// Horizontal padding: 9 px; reserve 16 px when HDF_SORTUP/HDF_SORTDOWN is set.
// Separators and bottom border: RGB(72,72,72), one physical pixel.
// Read every cell with Header_GetItemRect and HDITEMW{HDI_TEXT | HDI_FORMAT}.
// Determine the hot cell by GetCursorPos, ScreenToClient, and PtInRect.
// Draw the HDF_SORTUP/HDF_SORTDOWN indicator as a filled 7x4 triangle.
```

The subclass handles `WM_PAINT` with `BeginPaint`/`PaintDarkListViewHeader`/`EndPaint`, returns `1` for `WM_ERASEBKGND`, invalidates on `WM_MOUSEMOVE`, `WM_MOUSELEAVE`, `WM_LBUTTONDOWN`, `WM_LBUTTONUP`, and removes itself on `WM_NCDESTROY`. All unhandled messages call `DefSubclassProc`.

Implement the attachment helper as:

```cpp
void ApplyDarkListViewHeader(HWND listView) {
    if (!listView) {
        return;
    }
    HWND header = ListView_GetHeader(listView);
    if (!header) {
        return;
    }
    SendMessageW(header, WM_SETFONT, SendMessageW(listView, WM_GETFONT, 0, 0), TRUE);
    SetWindowSubclass(header, DarkHeaderSubclassProc, DARK_HEADER_SUBCLASS_ID, 0);
    InvalidateRect(header, nullptr, TRUE);
}
```

- [ ] **Step 3: Attach it to the blacklist table**

Immediately after inserting both blacklist columns, call:

```cpp
ApplyDarkListViewHeader(state->blacklistList);
```

Do not change blacklist row insertion, selection, removal, or persistence.

- [ ] **Step 4: Build and smoke-test the header**

```powershell
cmake --build build --config Release --target TextMagic
```

Expected: the build passes; the blacklist header is dark, labels and separators are visible, hover/press repaint cleanly, and both columns still resize.

- [ ] **Step 5: Commit the shared header**

```powershell
git add src/app/Application.cpp
git commit -m "fix: style application table headers"
```

---

### Task 3: Replace the running-application ListBox with a sortable ListView

**Files:**

- Modify: `src/app/Application.cpp:115-140` (`MessageWindowState`)
- Modify: `src/app/Application.cpp:5035-5295` (`MessageWindowProc`)
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`

**Interfaces:**

- Consumes: `CompareRunningApplications`, `ApplyDarkListViewHeader`, and stable source indices stored in `LVITEMW::lParam`.
- Produces: report-mode multi-select running-application picker with session-only `runningSortColumn` and `runningSortAscending` state.

- [ ] **Step 1: Add the missing localized column label**

Add these entries next to the existing blacklist column keys:

```ini
# lang/en.ini
application_blacklist.column.window_title=Window title

# lang/ru.ini
application_blacklist.column.window_title=Заголовок окна
```

Run:

```powershell
cmake --build build --config Release --target TextMagic
```

Expected: the build still passes; the new key is not visible until the ListView is implemented.

- [ ] **Step 2: Add session-only sort state and helpers**

Add to `MessageWindowState`:

```cpp
int runningSortColumn = -1;
bool runningSortAscending = true;
```

Add these helpers after the state declarations:

```cpp
int CALLBACK CompareRunningApplicationRows(
    LPARAM leftRow,
    LPARAM rightRow,
    LPARAM context
) {
    auto* state = reinterpret_cast<MessageWindowState*>(context);
    LVITEMW leftItem = {};
    leftItem.mask = LVIF_PARAM;
    leftItem.iItem = static_cast<int>(leftRow);
    LVITEMW rightItem = {};
    rightItem.mask = LVIF_PARAM;
    rightItem.iItem = static_cast<int>(rightRow);
    ListView_GetItem(state->textControl, &leftItem);
    ListView_GetItem(state->textControl, &rightItem);
    const auto& left = state->runningApplications[static_cast<size_t>(leftItem.lParam)];
    const auto& right = state->runningApplications[static_cast<size_t>(rightItem.lParam)];
    const int result = CompareRunningApplications(
        left,
        right,
        static_cast<RunningApplicationColumn>(state->runningSortColumn)
    );
    return state->runningSortAscending ? result : -result;
}

void SetRunningApplicationSortIndicator(HWND listView, int column, bool ascending) {
    HWND header = ListView_GetHeader(listView);
    const int count = Header_GetItemCount(header);
    for (int index = 0; index < count; ++index) {
        HDITEMW item = {};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, index, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (index == column) {
            item.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(header, index, &item);
    }
    InvalidateRect(header, nullptr, TRUE);
}
```

- [ ] **Step 3: Create and populate the native report ListView**

In `WM_CREATE`, keep the existing ListBox path only when `runningApplicationSelection == false`. For running applications create:

```cpp
state->usesListBox = !state->runningApplicationSelection;
state->textControl = CreateWindowExW(
    0, WC_LISTVIEWW, nullptr,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        LVS_REPORT | LVS_SHOWSELALWAYS,
    0, 0, 100, 100,
    hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_TEXT), GetModuleHandleW(nullptr), nullptr
);
ListView_SetExtendedListViewStyle(
    state->textControl,
    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP
);
ListView_SetBkColor(state->textControl, RGB(37, 37, 37));
ListView_SetTextBkColor(state->textControl, RGB(37, 37, 37));
ListView_SetTextColor(state->textControl, RGB(245, 245, 245));
```

Insert widths `170`, `280`, and `520` with keys `application_blacklist.column.application`, `application_blacklist.column.window_title`, and `application_blacklist.column.path`. Insert every source row with `LVIF_TEXT | LVIF_PARAM`, storing its original vector index in `lParam`, then set subitems 1 and 2 with `ListView_SetItemText`. Call `ApplyDarkScrollBar` and `ApplyDarkListViewHeader` after creation.

- [ ] **Step 4: Add native sorting and stable multi-selection extraction**

Handle `LVN_COLUMNCLICK` in `WM_NOTIFY`:

```cpp
auto* click = reinterpret_cast<NMLISTVIEW*>(lParam);
if (state && state->runningApplicationSelection &&
    click->hdr.hwndFrom == state->textControl &&
    click->hdr.code == LVN_COLUMNCLICK) {
    if (state->runningSortColumn == click->iSubItem) {
        state->runningSortAscending = !state->runningSortAscending;
    } else {
        state->runningSortColumn = click->iSubItem;
        state->runningSortAscending = true;
    }
    SetRunningApplicationSortIndicator(
        state->textControl,
        state->runningSortColumn,
        state->runningSortAscending
    );
    ListView_SortItemsEx(
        state->textControl,
        CompareRunningApplicationRows,
        reinterpret_cast<LPARAM>(state)
    );
    return 0;
}
```

Replace `LB_GETSELCOUNT`/`LB_GETSELITEMS` with:

```cpp
for (int row = ListView_GetNextItem(state->textControl, -1, LVNI_SELECTED);
     row != -1;
     row = ListView_GetNextItem(state->textControl, row, LVNI_SELECTED)) {
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (ListView_GetItem(state->textControl, &item)) {
        const size_t sourceIndex = static_cast<size_t>(item.lParam);
        if (sourceIndex < state->runningApplications.size()) {
            state->selectedApplicationPathsOut->push_back(
                state->runningApplications[sourceIndex].path
            );
        }
    }
}
```

Remove the running-picker `LBS_OWNERDRAWFIXED` row rendering branch. Keep `FillListBoxWithWrappedText` and `WM_CTLCOLORLISTBOX` for non-picker message windows. Draw the existing edit border whenever `state->textControl` exists so both control kinds retain the same frame.

- [ ] **Step 5: Build and run focused verification**

```powershell
cmake --build build --config Release --target TextMagic TextMagicRunningApplicationTests
ctest --test-dir build -C Release -R TextMagicRunningApplicationTests --output-on-failure
```

Expected: both targets pass. In the picker, all three columns render dark, resize, reorder, sort ascending/descending, preserve selected rows through sorting, and Add selected returns the correct paths.

- [ ] **Step 6: Commit the running-app table**

```powershell
git add src/app/Application.cpp lang/en.ini lang/ru.ini
git commit -m "feat: add sortable running application table"
```

---

### Task 4: Restore RichEdit colors and dark scrollbar without text overlap

**Files:**

- Modify: `src/app/Application.cpp:1064-1080` (`ApplyDarkScrollBar`)
- Modify: `src/app/Application.cpp:4560-4610` and `4695-4720` (log RichEdit setup order)

**Interfaces:**

- Consumes: existing optional dark-mode API pointers and RichEdit control.
- Produces: `ApplyDarkScrollBar(HWND control, bool applyExplorerTheme = true)` and post-font RichEdit formatting for existing/future log text.

- [ ] **Step 1: Reproduce the two visual failures**

Open the logs window in the current Release build and confirm: log glyphs are black on `RGB(45,45,45)` and the vertical scrollbar is light. Scroll and resize to retain the current no-overlap baseline.

- [ ] **Step 2: Make Explorer theming optional**

Change the helper signature and theme line to:

```cpp
void ApplyDarkScrollBar(HWND control, bool applyExplorerTheme = true) {
    if (!control) {
        return;
    }
    EnsureDarkScrollBarHookInstalled();
    if (g_allowDarkModeForWindow) {
        g_allowDarkModeForWindow(control, true);
    }
    if (applyExplorerTheme) {
        SetWindowTheme(control, L"Explorer", nullptr);
    }
    SendMessageW(control, WM_THEMECHANGED, 0, 0);
    SetWindowPos(
        control, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );
    RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}
```

Leave all existing call sites on the default `true` path.

- [ ] **Step 3: Apply log formatting after `WM_SETFONT`**

Remove the current early `EM_SETBKGNDCOLOR`/`EM_SETCHARFORMAT` block immediately after RichEdit creation. After the shared `WM_SETFONT` call, add:

```cpp
if (isLogs && state->richEdit && state->textControl) {
    SendMessageW(
        state->textControl,
        EM_SETBKGNDCOLOR,
        0,
        static_cast<LPARAM>(RGB(45, 45, 45))
    );
    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = RGB(245, 245, 245);
    SendMessageW(state->textControl, EM_SETCHARFORMAT, SCF_DEFAULT,
        reinterpret_cast<LPARAM>(&format));
    SendMessageW(state->textControl, EM_SETCHARFORMAT, SCF_ALL,
        reinterpret_cast<LPARAM>(&format));
    ApplyDarkScrollBar(state->textControl, false);
}
```

`SCF_DEFAULT` keeps appended lines light; `SCF_ALL` repairs the initial file contents after the font reset. The `false` argument avoids applying `Explorer` to the text surface.

- [ ] **Step 4: Build and perform the log regression smoke**

```powershell
cmake --build build --config Release --target TextMagic TextMagicLogFileTests
ctest --test-dir build -C Release -R TextMagicLogFileTests --output-on-failure
```

Expected: both targets pass. Generate many log lines, scroll, resize, close, and reopen the window; text remains light, the scrollbar remains dark, new lines inherit the same color, and no lines overlap.

- [ ] **Step 5: Commit the log fix**

```powershell
git add src/app/Application.cpp
git commit -m "fix: restore dark log control styling"
```

---

### Task 5: Release verification and handoff

**Files:**

- Verify: `build/bin/Release/TextMagic.exe`
- Verify: `src/app/Application.cpp`
- Verify: `src/app/RunningApplication.h`
- Verify: `tests/RunningApplicationTests.cpp`
- Verify: `lang/en.ini`, `lang/ru.ini`

**Interfaces:**

- Consumes: all prior tasks.
- Produces: a verified Release executable and exact handoff evidence.

- [ ] **Step 1: Run the clean Release build and complete suite**

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --clean-first
ctest --test-dir build -C Release --output-on-failure
```

Expected: `TextMagic.exe` builds and every registered CTest passes.

- [ ] **Step 2: Smoke-test all requested UI behavior**

Open the Release executable and verify:

- logs: light text, dark background/scrollbar, clean scrolling and resizing, no overlapping lines;
- blacklist: dark header, readable labels/separators, resizable columns;
- running applications: three dark columns, resize/reorder, per-column ascending/descending sorting, stable multi-selection, correct Add selected result;
- close and reopen the running picker: default widths/order and no persisted sort indicator.

- [ ] **Step 3: Inspect the final diff and branch state**

```powershell
git diff --check
git status --short --branch
git log --oneline --decorate -8
Get-Item build/bin/Release/TextMagic.exe | Select-Object FullName,Length,LastWriteTime
Get-FileHash build/bin/Release/TextMagic.exe -Algorithm SHA256
```

Expected: no whitespace errors; only the pre-existing untracked `.codebase-memory/` remains outside commits.

- [ ] **Step 4: Commit only verification corrections if required**

If the UI smoke reveals a defect, change only that defect, rerun its focused check plus the full Release suite, and commit the correction separately. Do not merge into `master` until the user explicitly requests it.
