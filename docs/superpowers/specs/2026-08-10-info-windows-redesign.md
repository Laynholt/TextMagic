# Redesign of the Logs and About windows

Date: 2026-08-10

## Goal

Restyle the existing Win32 information windows so that the Logs and About dialogs have a clearer hierarchy, darker inner panels, and interaction patterns consistent with the current TextMagic UI. The redesign must preserve existing application behavior while improving log selection and copying.

## Scope

The change covers only the `Logs` and `About` variants of the shared information window. The application blacklist window and unrelated dialogs remain unchanged.

## Existing context

- Both dialogs are created by `Application::CreateOrActivateInfoWindow` and handled by `Application::InfoWindowProc`.
- The application already has dark title bars, owner-drawn buttons and cards, styled context menus, and a dark scrollbar hook exposed through `ApplyDarkScrollBar`.
- Logs currently use a read-only RichEdit/Edit control and therefore select character ranges rather than complete entries.
- About content is currently assembled as one multiline string in `BuildAboutText`.

## Logs window

### Layout

- Use a wider, resizable window with proportions close to the approved reference image. The initial size should be approximately 900 by 600 logical pixels, subject to the existing DPI behavior and a practical minimum size.
- Draw one outer card using the existing TextMagic card treatment.
- Place the title `Logs` at the top of the card.
- Place a muted subtitle below it: users can select required rows or copy the complete current log.
- Place the log rows inside a separate rounded inner panel. Its background is darker than the outer card and uses the current TextMagic neutral palette.
- Place `Copy all` and `Close` buttons at the bottom right. Both use the existing neutral owner-drawn button style; the reference image's red close button is not copied.

### Log control and selection

- Replace the read-only text control for this window with an owner-drawn Win32 list box configured for extended multiple selection.
- Treat every persisted log line as one logical list item. Preserve the complete original text for copying even if the visible row is clipped by the current window width.
- A normal click selects one row.
- Ctrl-click adds or removes individual rows.
- Shift-click selects a contiguous range.
- Selection is drawn across the complete row with TextMagic-specific hover, focus, and selected colors.
- Ctrl+A selects every row.
- Ctrl+C copies the selected rows in their visual order, joined with Windows line endings.

### Copy and context menu behavior

- `Copy all` copies every current log row regardless of selection.
- The existing styled log context menu remains available.
- `Copy` copies selected rows and is disabled when the selection is empty.
- `Select all` selects every row.
- `Clear logs` keeps its current behavior and refreshes the open list immediately.
- Right-clicking an already selected row preserves a multi-row selection. Right-clicking an unselected row makes it the sole selection before opening the menu.

### Scrolling and updates

- Reuse the existing dark scrollbar implementation.
- Show the vertical scrollbar only when the number of rows exceeds the visible list capacity. Recalculate this after content changes and window resizing.
- Keep the list synchronized with newly appended log entries while the window is open.
- When the user is already at the bottom, appending a row keeps the newest row visible. Do not forcibly move a user who has scrolled upward.
- When the log is empty, show the localized empty-state text inside the inner panel and hide the scrollbar.

## About window

### Layout

- Use the same outer-card, spacing, typography, and inner-panel language as the Logs window.
- Keep the window non-maximizable and moderately increase its initial size to accommodate the new hierarchy without crowding.
- Place the title `About` at the top, followed by a short localized product description: TextMagic runs user text scenarios through hotkeys.
- Add a compact identity panel containing the `TextMagic` product name and a separate version badge.
- Add an information panel with aligned label/value rows:
  - Version
  - Loaded scripts
  - Scripts directory
- Allow the scripts-directory value to wrap and be copied through the existing copy-only context menu.
- Place a muted update-check hint below the information panel.
- Place `Check for updates` and `Close` at the bottom right using the existing owner-drawn button treatment.

### Dynamic data

- Read the version from the existing application version constant.
- Read the loaded-script count from the current script collection.
- Read the directory from the current scripts-directory setting.
- Refresh dynamic values whenever an existing About window is brought to the foreground.

## Shared implementation boundaries

- Extend the shared `InfoWindowState` only with handles and state needed by these two variants.
- Keep variant-specific creation, layout, and drawing separated into small helpers so the already-large `InfoWindowProc` does not accumulate all new behavior inline.
- Reuse `UiRenderer` primitives and current colors instead of creating a second visual system.
- Add localized Russian and English strings for the new subtitles, description, field labels, empty state, and copy-all button.
- Do not change the application blacklist layout or behavior.

## Error and edge-case handling

- Clipboard allocation or ownership failure must leave the application responsive and must not clear the current selection.
- Empty logs disable selection-dependent copy while keeping `Copy all` harmless.
- Log parsing must handle CRLF, LF, a final line without a newline, and an optional trailing newline without creating a spurious final item.
- Destroy all newly created brushes, fonts, or per-control state with the owning window.
- If the preferred control cannot be created, close creation cleanly rather than leaving a partially functional window.

## Testing and verification

### Automated tests

- Add tests for splitting persisted log text into logical rows.
- Add tests for joining selected rows in visual order.
- Add tests for empty input and trailing-newline handling.
- Add tests for the pure scrollbar-visibility calculation if it is extracted from the window procedure.

### Build and manual verification

- Build the complete CMake project in the supported Windows configuration.
- Run the complete existing test suite plus the new tests.
- Open the Logs window with empty, short, and overflowing logs.
- Verify single selection, Ctrl multi-selection, Shift range selection, Ctrl+A, Ctrl+C, context-menu copy, copy-all, and clear.
- Verify that the custom scrollbar is hidden for short content and visible for overflowing content.
- Resize the Logs window and confirm layout, selection, and scrollbar recalculation.
- Append logs while both scrolled to the bottom and scrolled upward.
- Open About and verify the dynamic version, script count, directory, directory copying, update action, and close action.
- Inspect both dialogs at the application's normal DPI and at one scaled-DPI setting for clipping and spacing regressions.

## Acceptance criteria

- Logs are selected as complete rows, including disjoint Ctrl selections and Shift ranges.
- Selected rows, all rows, and the scripts directory can be copied through the agreed controls.
- The existing styled context menu remains visually and functionally consistent.
- The dark scrollbar appears only when needed.
- Both windows use darker inner panels and the existing TextMagic color and button language.
- About presents version, loaded-script count, scripts directory, product description, and update hint as structured content.
- Existing update checking, log clearing, and window-closing behavior remains intact.
