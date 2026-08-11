# Running Picker and Surface Finish Design

Date: 2026-08-12

## Goal

Finish the remaining visual inconsistencies in the running-application picker, Application Blacklist table, and Logs content surface, then release the result as TextMagic `1.1.0`. The already approved appearance and behavior of all other windows must remain unchanged.

## Scope

This change covers:

- the in-card heading and native title bar of the running-application picker;
- the native light frame visible around the Application Blacklist and running-application ListViews;
- the visible corner treatment of the Logs list box;
- the project version generated from CMake.

The following are out of scope:

- changing the running-picker title text or localization;
- changing table columns, widths, sorting, selection, scrolling, or row colors;
- changing Logs typography, colors, selection, copying, context menu, append behavior, or scrollbar behavior;
- changing the approved About window, main window, MessageWindow text dialogs, buttons, or status card;
- introducing a general custom-control framework.

## Running-Application Picker

### In-card heading

Keep the existing localized `application_blacklist.running` text, including its current punctuation. Present it as a real section title matching the Application Blacklist title:

- use the shared large bold title font;
- title control height: `40 px`;
- gap below the title: `8 px`;
- retain the current outer inset and footer/button geometry;
- move the ListView down to make room for the larger heading;
- keep the picker window's current initial outer size unless minimum-size verification shows that the larger title requires a narrowly scoped height adjustment.

Generic MessageWindow dialogs retain their current compact title font and layout. The larger title branch applies only when `runningApplicationSelection` is active.

### Native title bar

Call the existing dark-title-bar helper immediately after the running-picker window is created and before it is shown. This must use the same DWM path and fallback already used by the main and info windows; no new title-bar implementation is needed.

## ListView Frame Finish

The white top and side lines are native themed non-client decoration, not part of the approved dark content palette. For the Application Blacklist and running-application ListViews:

- preserve the existing `RGB(37, 37, 37)` list fill and high-contrast text;
- preserve the custom dark header, column separators, scrollbars, and rounded parent frame;
- keep the scrollbar dark-mode hook but do not request the Explorer theme that introduces the light native edge;
- explicitly remove or suppress any remaining native client-edge/border styling if the theme opt-out alone does not eliminate the white frame;
- reapply the existing rounded child region after resize;
- do not change generic ListBox or MessageWindow styling.

The application-drawn outer border remains the only visible frame around these tables.

## Logs Corner Finish

The Logs content already uses a `10 px` rounded region, but its current one-pixel inset and matching child/parent fill make the corners appear square. Make the existing rounding visibly legible without changing the control rectangle:

- retain the `10 px` corner radius;
- introduce a Logs-only region inset larger than the shared default, initially `2 px`;
- keep the ListBox and empty-state control at their current full layout geometry;
- expose the already-painted parent border through the clipped corner pixels;
- leave the shared rounding used by all other approved surfaces unchanged.

If a rounded region cannot be created or assigned, keep the control usable with its previous region. Region ownership and cleanup must continue to follow `SetWindowRgn` rules.

## Version 1.1.0

Change the single project-version source in `CMakeLists.txt` from `1.0.0` to `1.1.0`. Continue generating runtime strings and Windows resource version fields from CMake; do not add duplicate hard-coded version constants. Update only tests or current documentation that intentionally assert the active version, leaving historical design records unchanged.

## Implementation Boundaries

- Prefer small mode-specific branches over global changes to shared MessageWindow or surface behavior.
- Reuse `ApplyDarkTitleBar`, `ApplyDarkScrollBar`, `ApplyDarkListViewHeader`, `ApplyRoundedChildRegion`, and existing renderer colors.
- If `ApplyRoundedChildRegion` needs an inset parameter, give it a default preserving all current callers and pass the larger inset only for Logs.
- Do not change list window styles, selection modes, item heights, keyboard routes, commands, or localization unless a native edge style is positively identified as the white-frame source.
- Do not combine this work with unrelated cleanup in `Application.cpp`.

## Verification

Automated coverage must verify:

- running-picker layout reserves `40 px` for the title plus the approved gap and does not overlap the ListView;
- generic MessageWindow layout metrics remain unchanged;
- the Logs-only rounding inset is larger than the shared default while preserving control geometry;
- the configured project version is `1.1.0` and generated version values remain consistent;
- existing layout, localization, blacklist, update, and resource-version tests remain green.

Integration checks must cover:

- running-picker dark native title bar, large title, table scrolling, column sorting, multi-selection, and both buttons;
- no white native frame around either table at normal and resized dimensions;
- visible rounded corners for the Logs list with and without enough rows to show its scrollbar;
- Logs selection, copy-all, context-menu copy, append behavior, and conditional scrollbar;
- no visual or behavioral regression in the other approved windows.

Run a fresh Release configure/build, the full CTest suite, and `git diff --check`. Attempt a safe native visual inspection. If native capture is blocked by the known Windows capture limitation, record that gap explicitly and report only the evidence actually obtained.
