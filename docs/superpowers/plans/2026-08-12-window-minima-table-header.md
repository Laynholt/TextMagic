# Window Minima and Application Table Header Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every app-owned window use its coded creation size as its minimum and prevent the shared application-table frame from covering header labels.

**Architecture:** Persist each final outer creation size in the corresponding window state and publish the same size from `WM_GETMINMAXINFO`. Consolidate the duplicated ListView initialization into one helper and replace the filling post-paint frame with a shared outline-only renderer.

**Tech Stack:** C++17, Win32, common controls, GDI/GDI+, CMake, CTest.

## Global Constraints

- Source default/minimum sizes from the exact values passed to `CreateWindowExW`; do not infer sizes from screenshots.
- Keep version `1.1.0`, localization, dark Explorer scrollbars, green selection, sorting, multi-selection, blacklist removal, and running-app path collection unchanged.
- Keep styled message windows visually non-resizable; an explicit stored minimum is policy only.
- Exclude dynamically measured popup menus, tooltips, and OS-owned file dialogs.
- Use strict RED→GREEN TDD; all file edits use `apply_patch`; all shell commands start with `rtk`.

---

### Task 1: Bind minimum track sizes to coded creation sizes

**Files:**
- Modify: `src/app/InfoWindowLayout.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces: `WindowOuterSize { int width; int height; }`, `ResolveMinimumOuterSize(WindowOuterSize)` returning the same size, and stored `minimumOuterSize` in `InfoWindowState`/`MessageWindowState`.

- [ ] **Step 1: Write failing size-policy tests**

Add assertions that `ResolveMinimumOuterSize({940,620})`, `{900,600}`, `{760,560}`, `{760,520}`, and `{500,230}` return exactly the supplied outer size. Include an About case using the outer result of its current 620x440 client design size.

- [ ] **Step 2: Run RED**

Run `rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests` and focused CTest. Expected: compile failure because `WindowOuterSize`/`ResolveMinimumOuterSize` do not exist.

- [ ] **Step 3: Implement the pure policy and state storage**

Add the pure type/helper. Add `minimumOuterSize` to both state structs. At every Info/Message creation site, compute the final width and height first, store that exact pair in the state, and pass the same pair to `CreateWindowExW`.

For About, replace the current client-to-outer `AdjustWindowRectEx` call with DPI-aware `AdjustWindowRectExForDpi` when available, using the owner/window DPI and the existing non-DPI fallback. Store its actual outer result.

- [ ] **Step 4: Route every minimum handler to the same source**

Main `WM_GETMINMAXINFO` uses named creation constants `940x620`, also used by main `CreateWindowExW`. `InfoWindowProc` returns `state->minimumOuterSize` for Logs/About/Blacklist. `MessageWindowProc` returns the stored size for running picker and generic styled messages; existing window styles remain unchanged.

Remove the smaller divergent LOGS/ABOUT/BLACKLIST/running-picker minimum branches and their now-unused minimum-only helpers/constants where safe.

- [ ] **Step 5: Verify GREEN**

Build `TextMagicInfoWindowLayoutTests` and `TextMagic`; run focused CTest, then full Release build and full CTest; run `rtk git diff --check`.

- [ ] **Step 6: Commit**

Commit exact message `fix: bind window minima to creation sizes`.

---

### Task 2: Share table setup and draw an outline-only frame

**Files:**
- Modify: `src/app/ContentSurfaceStyle.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`
- Test: `tests/RunningApplicationTests.cpp`

**Interfaces:**
- Produces: `enum class TableFramePaintMode { StrokeOnly }`, `ResolveTableFramePaintMode()`, `ApplicationTableColumn { const wchar_t* title; int width; }`, and `ConfigureApplicationTable(...)`.

- [ ] **Step 1: Write failing frame/setup contract tests**

Assert `ResolveTableFramePaintMode() == StrokeOnly`. Add pure assertions that blacklist column definitions preserve localized Application/Path order and running-picker definitions preserve Application/Window title/Path order and widths.

- [ ] **Step 2: Run RED**

Build/run focused layout and running-application tests. Expected: compile failure because the frame mode/shared column contract is absent.

- [ ] **Step 3: Implement shared `ConfigureApplicationTable`**

The helper performs the same sequence for both native ListViews: set dark colors/extended styles, strip native frame, insert supplied columns, assign `m_hFont`, apply the existing dark scrollbar for the supplied `ScrollbarSurface`, then call `ApplyDarkListViewHeader`.

Replace both duplicated setup blocks with calls to this helper. Keep only intentional caller differences: blacklist `LVS_SINGLESEL`, running picker `LVS_EX_HEADERDRAGDROP`, their different columns, and item population.

- [ ] **Step 4: Replace filling frame with stroke-only rendering**

Rewrite `DrawDarkListViewFrame` so it gets the real child window rectangle in parent coordinates and draws only the rounded outline with `kListBorder`/`kCornerRadius`. It must not call a helper that fills the ListView area, must restore selected GDI objects, and must release its DC. Both blacklist and running picker continue calling the same helper after `EndPaint`.

- [ ] **Step 5: Verify behavior paths remain unchanged**

Statically confirm shared green custom draw, blacklist removal enablement, running sort/multiselect/path extraction, header sort glyph, and both Explorer scrollbar calls remain wired.

- [ ] **Step 6: Verify GREEN and full Release**

Build `TextMagicInfoWindowLayoutTests`, `TextMagicRunningApplicationTests`, and `TextMagic`; run focused CTests, then full Release build, full CTest, and `rtk git diff --check`.

- [ ] **Step 7: Commit**

Commit exact message `fix: share application table rendering`.
