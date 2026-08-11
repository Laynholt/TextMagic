# About Softening and Text Metrics Design

## Goal

Polish the accepted About window without changing Logs, make version metadata visually secondary, soften the script-information card, and eliminate bottom clipping in About labels and the main `TextMagic` heading.

## Scope

- Logs is accepted and must not change.
- About keeps its current vertical information order, left alignment, copyable directory, update hint, and split footer buttons.
- The main window changes only enough to give its title font sufficient vertical space.

## Version metadata

- Replace the full-width version strip with a compact rounded chip directly beneath the description.
- The chip is fixed at 132 by 26 pixels at the current logical layout scale and remains left aligned with About content.
- Use fill and border `RGB(48, 48, 50)` so the chip has no visible outline and only slightly separates from the outer `RGB(45, 45, 45)` card.
- Keep the localized label and version value as separate controls inside the chip so the value continues to refresh normally.
- Use muted label text `RGB(160, 160, 165)` and secondary value text `RGB(205, 205, 210)`. Neither part uses bold or italic styling.
- The chip must not occupy the remaining row width and must not compete visually with the About title.

## Script-information card

- Keep one rounded card with `Loaded scripts` first and `Scripts directory` second.
- Change the card fill from `RGB(37, 37, 37)` to the softer `RGB(42, 42, 44)`.
- Use border and divider `RGB(55, 55, 58)`.
- Labels remain muted; count and directory values remain high contrast.
- Preserve the copy-only directory control and its context menu.

## About text clipping

- The current 18-pixel label rectangles are shorter than the rendered Segoe UI body-font metrics and visibly clip lower glyph portions.
- Increase About row-label height to 24 pixels and ordinary value height to 26 pixels.
- Increase the version-line height to 26 pixels and the details-card height to 174 pixels so the larger line boxes do not reduce spacing or overlap the divider.
- Derive the supported About minimum client height from all vertical sections rather than retaining the old 400-pixel literal. With the approved metrics, the minimum client height is 428 pixels.
- Continue using the existing DPI-aware non-client conversion so the larger client requirement is reflected in the outer minimum track height.

## Main title clipping

- Preserve the existing `-26`, semibold Segoe UI title font.
- Increase the main title control height from 34 to 40 pixels.
- Move the hint down by 6 pixels, from `innerY + 46` to `innerY + 52`.
- Move the list-top anchor down by the same 6 pixels, from `innerY + 102` to `innerY + 108`, preserving the current gap between the hint and script list.
- Buttons, card bounds, minimum window size, and the status card remain unchanged. The script list absorbs the six-pixel reduction in available height.

## Layout and painting

- `AboutWindowLayout::versionLine` becomes the chip rectangle and therefore has a width of 132 instead of the full content width.
- Paint the chip and information card in `WM_PAINT`; return matching brushes from `WM_CTLCOLORSTATIC` and `WM_CTLCOLOREDIT` to avoid rectangular color patches.
- Keep GDI resources stock or application-owned; do not introduce per-paint brush allocation.
- Preserve runtime localization, version refresh, loaded-script refresh, directory refresh, update action, and close behavior.

## Verification

- Add layout assertions for the 132-by-26 version chip, 24-pixel labels, 26-pixel values, 174-pixel information card, derived 428-pixel About client minimum, containment, and footer non-overlap.
- Verify the main title/hint/list offsets remain ordered after their six-pixel shift.
- Run a clean Release build, the full CTest suite, and `git diff --check`.
- Inspect the rebuilt About and main windows when native capture is available. If the host capture API again fails with `0x80004002`, report the limitation and rely on the supplied user screenshots, deterministic assertions, and read-only code review.

## Out of scope

- No changes to Logs, log colors, log interactions, main-window fonts, script-list typography, button styling, update logic, localization wording, or Windows display scaling.
