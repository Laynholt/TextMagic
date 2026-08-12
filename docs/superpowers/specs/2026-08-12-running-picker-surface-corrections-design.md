# Running Picker Surface Corrections Design

Date: 2026-08-12

## Status and Superseded Decisions

This addendum follows runtime inspection of commit `f940543`. The version bump and running-picker title sizing are accepted, but the first surface-finish approach did not satisfy the visual requirements.

This document supersedes these earlier decisions:

- ListView visual styles must not be disabled with `SetWindowTheme(control, L"", L"")`.
- The prior requirement to preserve table selection colors is replaced by the requested green selection treatment.
- A child-window region alone is not sufficient evidence of visible rounding; the outer arc and border must be visibly exposed in the rendered UI.

All requirements not explicitly replaced here remain in force.

## Goal

Remove the remaining white ListView frame/corner artifacts, restore TextMagic's existing dark scrollbars for both tables, give both tables a consistent green selected-row treatment, make Logs rounding visibly legible, and rename the running-picker heading to a clean localized title.

## Running-Picker Title

Add a title-specific localization key instead of reusing the action-button text:

- Russian: `Запущенные приложения`;
- English: `Running applications`.

The heading continues to use the approved large bold `40 px` title layout. The existing button text `Из запущенных...` / its English equivalent remains unchanged because it describes an action.

## Existing Dark Scrollbar Path

TextMagic's scrollbar helper is a themed hook, not a pixel-owner-drawn scrollbar. It redirects themed `ScrollBar` requests to `Explorer::ScrollBar`. Disabling visual styles prevents that route from running and produces the white classic scrollbars seen in the runtime screenshots.

For both the Application Blacklist ListView and the running-application ListView:

- remove the empty-theme `SetWindowTheme` calls;
- restore the default `ApplyDarkScrollBar(listView)` call;
- preserve all existing scroll styles and native scroll behavior;
- do not add a separate SCROLLBAR window or synchronize scroll positions manually.

The main and Logs ListBoxes retain their existing default scrollbar-helper calls.

## ListView Frame and Corner Treatment

Keep the native ListViews, headers, columns, sorting, selection state, and keyboard behavior. Add a narrowly scoped ListView surface subclass used only by the Application Blacklist and running picker.

The subclass must:

- delegate normal painting, input, scrolling, hit testing, notifications, focus, and destruction to the native control;
- after native non-client/client painting, cover only the light one-pixel frame and corner artifacts with the list surface color;
- draw the approved dark rounded outline using radius `10 px` and `RGB(62, 62, 62)`;
- refresh its frame after resize/theme changes;
- remove itself during `WM_NCDESTROY`;
- never intercept `WM_VSCROLL`, `WM_HSCROLL`, mouse, keyboard, sorting, or selection messages.

As a defensive invariant, clear only `WS_BORDER`, `WS_EX_CLIENTEDGE`, and `WS_EX_STATICEDGE` from the two ListViews and issue `SWP_FRAMECHANGED`. Preserve every other style bit. Use a table-specific `2 px` rounded-region inset so the application-painted outline remains visible.

## Green Table Selection

Both ListViews use one shared `NM_CUSTOMDRAW` row policy:

- normal fill: `RGB(37, 37, 37)`;
- normal text: `RGB(245, 245, 245)`;
- selected fill: `RGB(35, 105, 68)` (`#236944`);
- selected text: `RGB(255, 255, 255)`.

At prepaint, request item notifications. At item prepaint:

- query the actual `LVIS_SELECTED` state from the ListView row;
- choose the shared normal or selected colors;
- suppress only the native `CDIS_SELECTED` and `CDIS_HOT` paint flags so the system cannot overlay white or blue selection;
- retain `CDIS_FOCUS` so keyboard focus indication remains available;
- leave the real ListView selection state untouched.

This preserves inactive `LVS_SHOWSELALWAYS` selection, running-picker multi-selection, blacklist removal, selected-path collection, sorting, keyboard navigation, and focus behavior.

## Visible Logs Rounding

Keep the Logs ListBox's existing dark scrollbar and the `10 px` outer radius. The previous region-only change remains visually square because the child fills the same rectangle and obscures the outer panel.

Required rendering:

- keep the outer Logs panel at the existing `layout.content` rectangle;
- paint its border with the shared list border `RGB(62, 62, 62)`;
- move the ListBox and empty-state child to a rectangle inset by `2 px` on all sides;
- compute the inset rectangle with a pure clamped helper so width and height never become negative;
- reapply the rounded child region using dimensions of the inset child rectangle;
- keep row height, row rendering, selection, copying, context menu, append behavior, conditional scrollbar visibility, and button layout unchanged.

The two-pixel visual inset may reduce the native child client area by four pixels in each dimension. This is intentional and limited to exposing the outer rounded arc.

## Pure Contracts and Tests

Production UI mutation remains in the existing window procedures, while deterministic decisions are covered by pure helpers.

Automated coverage must verify:

- title localization uses the new heading key while the existing action-button key remains unchanged;
- frame-style stripping removes only `WS_BORDER`;
- extended-frame stripping removes only `WS_EX_CLIENTEDGE | WS_EX_STATICEDGE`;
- both style operations are idempotent and preserve child, visible, tab-stop, scrollbar, report, and selection-mode bits;
- selected and unselected row visual tokens resolve to the exact approved colors;
- selected custom-draw policy suppresses `CDIS_SELECTED | CDIS_HOT` but preserves `CDIS_FOCUS`;
- a `100 x 80` surface inset by `2 px` resolves to `{2, 2, 96, 76}` in x/y/width/height form;
- tiny surfaces clamp inset width and height to zero rather than producing negative dimensions;
- both table modes resolve to the default themed scrollbar policy;
- the generic MessageWindow and Logs scrollbar policies remain unchanged;
- the configured version remains `1.1.0` and the full existing suite remains green.

Do not add source-text grep tests for Win32 calls. Native theme/subclass integration is covered by build evidence, targeted code review, and runtime inspection.

## Failure Handling and Ownership

- If subclass installation fails, leave the native ListView usable and log or report the cosmetic failure without disabling the dialog.
- If rounded-region creation or assignment fails, preserve the usable rectangular control and follow existing `HRGN` ownership rules.
- Use stock or existing state-owned drawing resources where possible; any new owned GDI resource must have an explicit teardown path.
- A cosmetic paint failure must never disable scrolling, selection, sorting, copying, or commands.

## Verification

Run strict RED -> GREEN for each new pure contract. Then run:

- focused layout/localization tests;
- Release build of `TextMagic`;
- full Release CTest suite, including the version test;
- `git diff --check` and clean-status verification.

Runtime verification must cover:

- heading reads `Запущенные приложения` without an ellipsis;
- both ListViews use the existing dark TextMagic scrollbar;
- no white line or white corner artifact remains around either table;
- selected rows are dark green with white text in both tables, including inactive selection and multi-selection;
- blacklist add/remove, running-picker sorting/add/cancel, keyboard navigation, and both scroll directions still work;
- Logs visibly shows a rounded `RGB(62, 62, 62)` outline with and without its vertical scrollbar;
- Logs selection/copy/context menu/append/conditional-scrollbar behavior remains intact.

If automated native capture remains blocked, record the limitation. The user's runtime screenshots are valid evidence that the previous design failed, but the corrected result must not be claimed visually verified until a new runtime inspection succeeds.
