# Reliable text replacement and hotkey exclusions window

## Goal

Make tracked-text replacement reliable in regular Windows text editors and move
the fullscreen-hotkey setting into a small window that can later host application
exclusions.

## Replacement behavior

- Use repeated Backspace for captures up to and including 100 UTF-16 code units.
- For captures longer than 100 units, hold Shift while sending Left presses
  incrementally, release Shift only after the target application has had time to
  process the selection, delete the selection once, then type the script result.
- If tracked input is still valid for the current foreground context, use it
  before probing the application with Ctrl+C. This avoids treating editor
  features such as “copy current line without a selection” as an explicit
  selection.
- Physical mouse or keyboard selection already resets the tracked-input session,
  so a real user selection continues to use the selected-text path.
- Abort replacement if deletion or selection fails; do not type the transformed
  result on top of text that was not removed.
- Keep direct keyboard input for tracked-text replacement. Do not use the
  clipboard for deletion or insertion.

## Hotkey exclusions window

- Replace the checked menu item with a regular menu command named
  “Hotkey exclusions…” / “Исключения горячих клавиш…”.
- The command opens one modeless, owner-associated window. Repeated activation
  brings the existing window forward instead of creating duplicates.
- The window initially contains only the existing
  “Disable hotkeys in fullscreen applications” checkbox and a Close button.
- Toggling the checkbox updates and persists the current setting immediately.
- Closing the window does not change the saved value.
- No process list, executable picker, or blacklist storage is added in this
  change.

## Tests and verification

- Add focused checks for the 100/101 replacement boundary.
- Add focused checks that valid tracked input wins over a clipboard selection
  probe and that selection remains the fallback when no tracked input exists.
- Keep the existing partial-input cleanup tests.
- Run the complete Debug build and CTest suite.
- Manually verify replacement in Notepad and Sublime Text when desktop
  automation is available.
