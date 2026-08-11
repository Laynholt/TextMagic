# Content Surface Polish Design

Date: 2026-08-11

## Goal

Polish the remaining large content surfaces so they match the established TextMagic UI, fix the clipped Application Blacklist title, and simplify the main-window guidance. The existing Logs and About designs are approved and must remain visually and behaviorally unchanged.

## Scope

This change covers:

- the text surface inside styled MessageWindow dialogs;
- the Application Blacklist window layout and list view;
- the running-application selection list view;
- the main script list surface;
- other large list/text surfaces hosted by MessageWindow;
- the main-window hint text, font, and color;
- Russian and English localization for the revised hint.

The following are out of scope:

- Logs colors, typography, geometry, selection, copying, context menu, append behavior, or scrollbar behavior;
- About colors, typography, geometry, directory field, and button layout;
- List-view columns, header dividers, sorting behavior, or column widths;
- button styling and status-card styling;
- a broad UI-control framework refactor.

## Visual System

### Shared large-content radius

Use the current Logs content radius as the reference:

- radius: `10 px`;
- apply it to the main script list, Application Blacklist list view, running-application selection list view, MessageWindow text/list surface, and other large MessageWindow content controls;
- refresh the child window region after every resize;
- preserve the existing surface colors for the main script list and table/list views;
- preserve Logs and About exactly as they are.

Small fields, including the About scripts-directory field, are not large content surfaces and remain unchanged.

### MessageWindow content surface

The MessageWindow content bubble must be softer and closer to the surrounding card:

- fill: `RGB(42, 42, 44)`;
- border: `RGB(55, 55, 58)`;
- radius: `10 px`;
- text color remains the current high-contrast content text;
- wrapped text, scroll behavior, copying, context-menu behavior, and buttons remain unchanged.

The MessageWindow surface brush must be owned by the window state and released with the rest of the state-owned GDI resources. If a rounded region cannot be created or assigned, the control remains usable with its rectangular system region.

### Other large surfaces

The main script list, blacklist list view, and running-application list view keep their existing fills and text colors. Only their child regions and outer border treatment become rounded. Existing header and column separators remain visible and unchanged.

## Application Blacklist Layout

The current title uses the large shared title font but receives only `26 px` of control height. The checkbox begins six pixels below that box, causing descender clipping and overlap. Fix the source geometry instead of changing the title font.

Required layout:

- outer inset: `16 px`;
- title control height: `40 px`;
- gap below title: `8 px`;
- fullscreen checkbox height: `28 px`;
- the list begins after a separate gap below the checkbox;
- the list remains above the footer with the existing footer gap;
- bottom buttons retain their current order, widths, alignment, and behavior;
- initial outer window width remains `760 px`;
- initial outer window height increases from `520 px` to `560 px`;
- minimum outer width remains `640 px`;
- minimum outer height increases from `420 px` to `460 px`.

The layout calculation should be represented by a pure helper so minimum-size non-overlap can be verified without a native window.

## Main-Window Guidance

Remove the redundant introduction "Global text scripts" / "Глобальные скрипты для текста" and the right-click management note. Present only the operational guidance requested by the user.

Russian text:

```text
Скрипты работают с выделенным текстом, последним введённым словом или всем введённым текстом.
Двойной щелчок применяет выбранный скрипт к тексту из буфера обмена.
```

English text:

```text
Scripts work with selected text, the last typed word, or all typed text.
Double-click a script to apply it to text from the clipboard.
```

Presentation:

- Segoe UI regular, `-14` logical height, matching the supporting text in Logs/About;
- text color: `RGB(170, 170, 175)`;
- retain the existing two-line control and the approved main-header geometry;
- keep the TextMagic title, script-list position, buttons, and status card unchanged.

The text is behaviorally accurate: the list double-click route continues to call the selected script in clipboard-only mode.

## Implementation Boundaries

- Reuse the existing rounded-child-region implementation already used by Logs.
- Use the existing high-quality rounded-panel GDI+ path for border painting instead of adding a new renderer.
- Replace rectangular border drawing only for the large surfaces in scope.
- Reapply regions after `MoveWindow` so resizing does not leave stale geometry.
- Do not change list selection modes, list item heights, ListView extended styles, keyboard routes, context menus, scrollbars, sorting, or double-click commands.
- Do not combine this work with unrelated refactoring of `Application.cpp`.

## Failure Handling

- If `CreateRoundRectRgn` fails, leave the control region unchanged.
- If `SetWindowRgn` fails, delete the unowned region and leave the control usable.
- GDI brushes created for MessageWindow must be deleted during normal window-state teardown.
- A failure to apply cosmetic rounding must never disable selection, scrolling, copying, sorting, or script execution.

## Verification

Automated coverage must include:

- Application Blacklist title and checkbox do not overlap;
- blacklist list and footer do not overlap at the minimum layout height;
- blacklist initial/minimum sizing constants match the approved values;
- the main header helper retains valid hint/list spacing with the supporting font change;
- Russian and English `hint.label` values contain the approved concise guidance and no longer contain the removed introduction;
- existing layout, localization, blacklist, clipboard, and script-input tests remain green.

Integration checks must cover:

- main-list selection, double-click execution, context menu, and conditional scrollbar;
- blacklist row selection, add/remove actions, checkbox, resizing, and list scrolling;
- running-application selection, column sorting, multi-selection, and scrolling;
- MessageWindow wrapping, copying, context menu, buttons, and scrolling;
- no visible or behavioral change in Logs or About.

Run a clean Release build, the full CTest suite, and `git diff --check`. Attempt a safe native visual inspection. If native capture is blocked again by `SetIsBorderRequired ... 0x80004002`, record the visual verification gap explicitly and rely only on the automated and accessibility evidence actually obtained.
