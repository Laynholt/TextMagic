# Surface Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reuse the working table and rounded-list rendering pipelines so the blacklist has no white edge and Logs has smooth corners matching the main list.

**Architecture:** Add pure shared surface contracts in `ContentSurfaceStyle.h` and small HWND paint helpers in `Application.cpp`. Keep native controls and behaviors; unify only geometry, initialization order, and parent frame ownership.

**Tech Stack:** C++17, Win32 `LISTBOX`/`WC_LISTVIEW`, common controls subclassing, GDI+, CMake/CTest.

## Global Constraints

- Preserve dark Explorer scrollbars, green table selection, sorting, multiselect, copy/context-menu behavior, conditional Logs scrollbar, localization, and version `1.1.0`.
- Do not modify `tests/ScriptExecutionGateTests.cpp` or `tests/ScriptManifestTests.cpp`; the user's uncommitted main-worktree edits own those files.
- All edits use `apply_patch`; all shell commands start with `rtk`.
- Use strict RED→GREEN TDD for each task.

---

### Task 1: Share the working table frame path

**Files:**
- Modify: `src/app/ContentSurfaceStyle.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces: `TableFrameOwner::ParentAfterChild`, `ResolveTableFrameOwner()` and a shared `DrawDarkListViewFrame(HWND parent, HWND listView)` wrapper.

- [ ] **Step 1: Write the failing contract test**

Assert that the table frame owner is `ParentAfterChild`, a stronger contract than the existing generic `Parent` enum.

- [ ] **Step 2: Run RED**

Run `rtk cmake --build build --config Release --target TextMagicInfoWindowLayoutTests` and the focused CTest. Expected: compile failure because `ParentAfterChild` is absent.

- [ ] **Step 3: Implement the shared working path**

Replace the existing table owner enum value with `ParentAfterChild`. Add `DrawDarkListViewFrame` that calls `UiRenderer::DrawRoundedControlFrame(parent, listView, 0, kListFill, kListBorder, kCornerRadius)`.

In both parent `WM_PAINT` handlers, finish `BeginPaint/EndPaint` first and then call this helper. Remove the blacklist-only inline `DrawRoundedPanel(layout.list, ...)`. Keep both ListViews' themes, regions, headers, and custom draw unchanged. In blacklist creation, move its ListView `WM_SETFONT` before `ApplyDarkListViewHeader`, matching the running picker.

- [ ] **Step 4: Verify GREEN**

Build `TextMagicInfoWindowLayoutTests`, `TextMagicRunningApplicationTests`, and `TextMagic`; run the two focused CTests and `rtk git diff --check`.

- [ ] **Step 5: Commit**

Commit exact message `fix: share working table frame path`.

---

### Task 2: Reuse the main rounded-list geometry for Logs

**Files:**
- Modify: `src/app/ContentSurfaceStyle.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/InfoWindowLayoutTests.cpp`

**Interfaces:**
- Produces: `kRoundedListContentPadding = 6`, `kRoundedListRegionInset = 1`, and `kCornerRadius = 10` as the single shared main/Logs geometry.

- [ ] **Step 1: Write the failing geometry tests**

Assert the shared 6 px padding and 1 px region inset, and verify a 100x80 outer rect produces a 6,6,88,68 child rectangle without a second inset.

- [ ] **Step 2: Run RED**

Run the focused layout target/CTest. Expected: compile failure because the new shared constants/helper contract is absent.

- [ ] **Step 3: Implement one geometry pipeline**

Add a pure `InsetSurfaceRect(SurfaceRect, int)` helper if not already present and use it once with `kRoundedListContentPadding` for both Logs children. Apply `ApplyRoundedChildRegion` with the shared `kCornerRadius` and `kRoundedListRegionInset`.

Remove Logs-only `kLogsCornerRadius`, `kLogsRegionInset`, and independent `DrawRoundedPanel(layout.content, ...)`. After `EndPaint`, call `UiRenderer::DrawRoundedControlFrame` using the visible `logList` or `emptyLabel`, padding 6, shared list colors, and radius 10 — the same pattern as the main list.

- [ ] **Step 4: Verify GREEN and full Release**

Build the focused layout target and `TextMagic`, run focused CTest, then full Release build, full CTest (expected 19/19), and `rtk git diff --check`.

- [ ] **Step 5: Commit**

Commit exact message `fix: share main rounded list geometry`.
