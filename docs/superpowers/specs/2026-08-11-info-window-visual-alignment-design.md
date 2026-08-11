# Info Window Visual Alignment Design

## Goal

Refine the existing Logs and About windows so they visually belong to the same UI system as the TextMagic main window. Preserve all existing behavior, localization, selection, copying, context menus, update checks, script refreshes, custom scrolling, resizing, and DPI handling.

## Shared visual language

- Reuse the main script-list surface color, `RGB(37, 37, 37)`, instead of the near-black `RGB(24, 24, 26)` surfaces introduced by the first redesign.
- Keep the existing outer card color and window background used by the main screen. Inner borders remain subtle and use the application's existing neutral border colors.
- Information-window titles use a larger Segoe UI semibold font comparable to the main `TextMagic` heading.
- Supporting descriptions use a smaller regular Segoe UI font and a muted neutral text color. They are not italic.
- Body values remain high-contrast and left aligned.
- Existing custom buttons, hover states, selection colors, and dark scrollbar behavior remain unchanged.

## Logs window

### Header

- `Логи выполнения` / `Execution logs` becomes visibly larger and semibold.
- The selection/copy hint becomes slightly smaller than ordinary UI text and uses a muted gray color.
- The spacing between the title, hint, and log surface is tightened into a clear hierarchy without reducing readability.

### Log surface

- The log-list fill changes to the same `RGB(37, 37, 37)` surface used by the main script list.
- The rounded parent panel and child clipping remain in place, but the panel no longer appears as a separate near-black block.
- Row typography, multi-selection, Ctrl/Shift selection, Ctrl+A, Ctrl+C, context actions, Copy All, append behavior, and conditional custom scrollbar are unchanged.

### Footer

- `Copy all` and `Close` keep their existing positions and behavior.
- Footer spacing continues to obey the tested minimum-client-height invariant.

## About window

### Header and product information

- `О программе` / `About` becomes larger and semibold.
- The TextMagic description becomes smaller and muted.
- Remove the separate identity card containing `TextMagic` and the version badge.
- Show a compact localized version line beneath the description, left aligned: `Версия 1.0.0` / `Version 1.0.0`.

### Script information card

- Use one neutral rounded card matching the main list surface.
- Present two vertically stacked, left-aligned information groups in this order:
  1. muted label `Загружено скриптов` / `Loaded scripts`, followed by the script count on its own value line;
  2. muted label `Каталог скриптов` / `Scripts directory`, followed by the copyable directory path on its own value line.
- Separate the groups with spacing and a subtle horizontal divider.
- The count is never pinned to the right edge.
- The directory remains a read-only copyable control and may ellipsize visually when the window is narrow without changing the copied value.

### Update area and footer

- Keep the update hint beneath the information card in the same muted supporting style.
- Anchor `Проверить обновления` / `Check for updates` to the left side of the footer.
- Anchor `Закрыть` / `Close` to the right side.
- Preserve update-check and close behavior.

## Layout and resizing

- Update `AboutWindowLayout` to remove identity-card and version-badge geometry and expose rectangles for the version line, unified information card, two stacked label/value groups, divider, hint, and footer buttons.
- Preserve the established safe minimum client size and DPI-aware minimum track sizing.
- Long localized strings and long script paths must stay inside their assigned rectangles at the minimum supported size.
- Logs geometry changes only as needed for the new title/subtitle font metrics and spacing; the tested content/footer non-overlap invariant remains intact.

## Localization and dynamic updates

- Reuse existing localization keys where their wording remains correct. Add or adjust keys only when required by the simplified version line or label ordering.
- Runtime language switching updates all open controls.
- Script count and directory refresh behavior remains dynamic while About is open.

## Testing and verification

- Update layout tests to assert the new About vertical order, left alignment, containment, divider placement, button anchoring, and minimum-size non-overlap.
- Retain Logs model and interaction tests; add or update layout assertions only where header metrics change.
- Run a clean Release build, the complete CTest suite, and `git diff --check`.
- Inspect both windows in the newly built executable against the supplied screenshots when native UI capture is available. If capture remains unavailable because of the host Computer Use error, report that limitation explicitly rather than changing system settings or bypassing the application's singleton guard.

## Out of scope

- No changes to log persistence, log context-menu commands, update logic, script discovery, main-window layout, global button styling, or system display scaling.
