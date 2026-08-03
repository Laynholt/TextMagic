# Dark Tables and Logs Design

## Goal

Restore the logs window's dark appearance without reintroducing overlapping text, style ListView headers consistently with TextMagic, and replace the running-application picker's combined text rows with a sortable three-column table.

## Scope

- Logs window: light text, dark background, and dark vertical scrollbar.
- Application blacklist: dark column header matching the existing palette.
- Running-application picker: resizable and reorderable Application, Window title, and Path columns with click sorting.
- Column widths, order, and sort state exist only while the picker is open.

No new dependency or persistent UI configuration is required.

## Design

### Logs

Keep the RichEdit control because its append behavior prevents the earlier overlapping-line repaint bug. Apply the log font first, then set both the default character format and all existing text to the application's light text color. This preserves the color for file reloads and later appended log entries.

Reuse the existing dark-scrollbar initialization and `AllowDarkModeForWindow` path, but do not force the `Explorer` theme onto the RichEdit text surface. Refresh only the control theme/frame needed for its scrollbar.

### Dark ListView headers

Use one small reusable header styling path for both blacklist and running-application ListViews. Explicitly paint the header background, labels, separators, and hover/pressed states with the current TextMagic palette instead of relying on Windows theme names, whose result varies by Windows version.

The ListView body keeps its existing custom-draw colors and native scrollbar behavior.

### Running-application picker

Use a report-mode multi-select ListView instead of the owner-drawn ListBox. Add three localized columns:

1. Application executable name.
2. Window title.
3. Executable path.

Enable native full-row selection, double buffering, header drag-and-drop, and adjustable column widths. Store each row's stable source index in `LVITEM::lParam`, then use native `ListView_SortItemsEx` when a header is clicked. Clicking the same column toggles ascending/descending order; selecting a new column begins ascending. Native item sorting preserves row selection.

The Add selected button resolves selected rows through their stable indices, so existing deduplication and blacklist persistence remain unchanged.

## Error Handling

- Missing executable paths remain displayable and selectable exactly as the current discovery layer provides them.
- If the RichEdit or ListView control cannot be created, retain the existing dialog failure/fallback behavior.
- Dark-mode APIs remain optional; failure to load them must not prevent the windows from opening.

## Verification

- Add a focused check for running-application comparisons across all three columns and both directions.
- Run the existing test suite and a clean Release build.
- Smoke-test the logs, blacklist, and running-application windows: colors, scrolling, column resize/reorder, ascending/descending sorting, multi-selection, and adding selected applications.
