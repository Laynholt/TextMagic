# Surface Parity Design

## Goal

Remove the remaining white ListView outline in the application blacklist and the stepped corners in Logs by reusing the already-correct rendering paths instead of maintaining parallel geometry and paint implementations.

## Blacklist table

The blacklist and running-application controls remain native `WC_LISTVIEW` controls with their current Explorer dark scrollbar theme, shared dark header, green selection custom draw, sorting/selection behavior, and rounded child region.

Both windows will share one parent-owned table-frame helper. It will call `UiRenderer::DrawRoundedControlFrame` from the real ListView HWND after `EndPaint`. The blacklist's separate `DrawRoundedPanel(layout.list, ...)` call inside `BeginPaint` will be removed. This gives the blacklist the same paint ordering as the working running-application picker, so the dark frame overlays any late native edge.

Blacklist initialization will also follow the working order: create/configure columns, apply the scrollbar theme, set the ListView font, then style the header.

## Logs surface

Logs and the main script surface remain native owner-drawn `LISTBOX` controls. Logs retains its extended multi-selection, owner draw, hover subclass, Ctrl+A/C, context menu, copy actions, empty-state overlay, conditional scrollbar, and colors.

Logs will use the main list's single geometry pipeline:

- outer-to-child padding: 6 px;
- child rounded-region inset: 1 px;
- corner radius: 10 px;
- parent frame derived from the actual visible child HWND with `UiRenderer::DrawRoundedControlFrame` after normal parent painting.

The Logs-only double inset and independent 14 px panel geometry will be removed. Both `logList` and `emptyLabel` use the same child rectangle and region. The parent frame is drawn from whichever child is visible, so its frame and clipping geometry cannot drift apart.

## Verification

Pure layout/style contracts cover the shared table-frame ownership and the 6/1/10 rounded-list geometry. Focused tests run RED before production changes. A fresh Release build and the full CTest suite run after implementation. Manual checks cover an empty blacklist, populated/scrolling running picker, Logs with and without scrollbar, and Logs empty state when native capture is available.
