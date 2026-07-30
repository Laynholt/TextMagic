# Typed-text modes and list padding design

## Purpose

Make the no-selection input modes match their names:

- **Last typed word** transforms the last word in the current physical-input session.
- **All typed text** transforms the complete current physical-input session.

Also add a small visual inset around the script list and log list content.

## Input session

`InputBuffer` remains one process-wide object backed by `std::wstring`. Its maximum
tracked size becomes 20,000 UTF-16 code units.

The existing reset rules remain unchanged: input context change, physical mouse
click, navigation/editing keys, Enter, Tab, Escape, function keys, and untracked
Ctrl/Alt/Win combinations clear the session. Backspace removes the last tracked
character. Injected TextMagic input is ignored.

If appending text would exceed 20,000 code units, the whole session is cleared.
The buffer must not silently retain only the tail.

## Mode behavior

The menu labels become:

- Russian: `Последнее введённое слово` and `Весь введённый текст`.
- English: `Last typed word` and `All typed text`.

Existing persisted values `previous_word` and `all_text` remain unchanged for
settings compatibility.

When a script is invoked by hotkey:

1. An explicit selection continues through the existing selected-text path.
2. Without a selection, `previous_word` captures the last word and its trailing
   separators.
3. Without a selection, `all_text` captures the complete current input session.
4. The captured context and buffer revision are checked again before replacement.

After a successful replacement, the buffer is updated to the replacement so a
following script can process the new text. A stale capture aborts without changing
the target application.

## Replacement strategy

The strategy depends only on the number of characters to remove and therefore
applies to both modes:

- Up to and including 500 characters: send the existing repeated Backspace input.
- More than 500 characters: select exactly that many preceding characters with
  `Shift+Left`, send one Backspace, then type the script result.

The long selection is sent as one native `SendInput` batch. Word-based
`Ctrl+Shift+Left` is not used because word boundaries differ between applications.
`Shift+Home` is not used because the tracked session may begin in the middle of an
existing line.

Before either path, TextMagic waits for physical modifiers to be released and
verifies that the capture is still current. If long selection is incomplete,
TextMagic releases Shift, collapses the selection at its original right edge,
clears the input session, and aborts without deleting text. If typing the result
fails after deletion, the existing cleanup-and-restore behavior restores the
captured source text where possible.

Clipboard paste is deliberately not introduced for this path: direct keyboard
input preserves the terminal-compatible behavior and does not disturb clipboard
contents.

## List padding

Keep the native list boxes. Reserve a small inset between each list box and its
existing custom border in layout:

- script list in the main window;
- log list in the logs window.

The parent background provides the visible padding. This avoids owner-drawing list
items and preserves native selection, scrolling, keyboard navigation, context
menus, and accessibility behavior.

## Fullscreen hotkey suppression

Add an unchecked-by-default option to the More menu:

- Russian: `Отключать горячие клавиши в полноэкранных приложениях`.
- English: `Disable hotkeys in fullscreen applications`.

Persist the option in the existing INI settings file. When enabled, both native
and hook-based script hotkeys stop before script execution if the foreground
window covers its monitor's complete monitor rectangle, including the taskbar
area. A normal maximized window that only covers the monitor work area is not
treated as fullscreen.

The check uses the foreground window and its nearest monitor. TextMagic's own
windows, the desktop, invalid/minimized windows, and ordinary maximized windows
are not fullscreen targets. Suppression performs no clipboard access, script
launch, status change, or log entry.

An application executable blacklist is a possible later extension of the same
pre-execution gate, but process enumeration, executable pickers, storage, and UI
for that feature are outside this change.

## Verification

- Unit-test complete-session capture, replacement commit, Backspace behavior, and
  full reset on 20,000-character overflow.
- Unit-test the 500-character replacement-strategy boundary.
- Unit-test fullscreen rectangle classification, including a true fullscreen
  rectangle and a normal maximized work-area rectangle.
- Build the Debug configuration and run the existing CTest suite.
- Manually verify both modes and both replacement paths in a normal editor and
  PowerShell.
- Visually verify script-list and log-list padding.
- Manually verify that the enabled fullscreen option suppresses a script hotkey
  in a fullscreen application but not in a normal maximized window.

## Alternatives rejected

- Repeated Backspace for all 20,000 characters: reliable but needlessly slow for
  long sessions.
- Word-wise selection: faster but cannot select an exact cross-application range.
- Clipboard replacement: faster for very large text but weakens terminal behavior
  and clipboard isolation.
- Owner-drawn list boxes: more code than required for a small visual inset.
- Building the executable blacklist now: speculative UI and persistence that are
  unnecessary for fullscreen suppression.
