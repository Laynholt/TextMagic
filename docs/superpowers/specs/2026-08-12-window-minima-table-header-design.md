# Window Minima and Application Table Header Design

## Goal

Use each app-owned window's current creation size from code as its minimum size, and make the blacklist/running-application tables share one safe setup and frame implementation that cannot cover header text.

## Minimum-size policy

The source of truth is the exact outer width and height passed to `CreateWindowExW`, not screenshot measurements and not smaller layout-derived constants.

- Main window: reuse its current coded creation width and height as `WM_GETMINMAXINFO` minimum track size.
- Logs, About, and Blacklist: store the exact final outer size used for creation in `InfoWindowState` before `CreateWindowExW`; `InfoWindowProc::WM_GETMINMAXINFO` returns that stored size for every kind.
- Running-application picker and app-owned styled message windows: store the exact outer size used for creation in `MessageWindowState`; `MessageWindowProc::WM_GETMINMAXINFO` returns it. Styled message windows remain visually non-resizable because their existing styles are unchanged.
- About keeps its current client design size, but its client-to-outer conversion becomes DPI-aware (`AdjustWindowRectExForDpi` with the existing fallback). The resulting outer size is stored and becomes the minimum.
- Dynamic popup menus, tooltips, and OS-owned file dialogs are excluded because they have no fixed app-owned default/minimum pair.

The creation call and minimum handler consume the same stored/named value so future changes cannot drift.

## Shared application-table setup

Blacklist and running picker remain native `WC_LISTVIEW` controls. A shared `ConfigureApplicationTable` helper owns their common sequence:

1. set dark list colors and extended ListView styles;
2. strip native frame styles;
3. insert the supplied columns;
4. assign the shared UI font;
5. apply the existing Explorer dark scrollbar path;
6. install the existing dark header subclass.

Callers supply only the column definitions, scrollbar surface kind, and the small deliberate style differences (`LVS_SINGLESEL` for blacklist and `LVS_EX_HEADERDRAGDROP` for running picker). Green selection custom draw, sorting, multi-selection, removal, and path collection stay unchanged.

## Safe table frame

The current `DrawDarkListViewFrame` is unsafe because it calls `DrawRoundedControlFrame`, which fills the entire ListView rectangle after the child has painted. That fill covers the header text in an empty blacklist and creates a latent paint-order race in the running picker.

The shared table frame helper will draw only the rounded outline (stroke) over the child rectangle. It must not fill the ListView client/header surface and must release/restore all GDI resources. Both windows call the same helper after `EndPaint`, so late native edge pixels are covered without covering header text or rows.

## Verification

- Pure tests assert creation-size/minimum equality policies and table frame mode `StrokeOnly`.
- Focused tests run RED before implementation.
- Release build and full CTest run after both tasks.
- Native smoke checks cover: empty blacklist header labels, populated running picker after scroll/resize, every resizable window refusing sizes below its coded default, and About at current DPI when capture is available.
