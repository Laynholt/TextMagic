# Generic Hotkey Layout Cycle Design

## Goal

Add a bundled script that cycles the foreground application's installed keyboard layouts. The application must not hardcode Shift, Ctrl, or any other binding for this action. The script manifest owns the hotkey, so changing only the script can move the action from `Shift` to `Ctrl`, `Ctrl+Shift`, `Ctrl+Alt+L`, or another supported combination.

## Script contract

The bundled script is a manifest-only built-in action:

```text
name=Next Keyboard Layout
description=Switches to the next installed keyboard layout
hotkey=Shift
action=cycle_keyboard_layout
enabled=true
```

`action=cycle_keyboard_layout` requires neither a command nor an inline script body. It bypasses text capture, clipboard access, PowerShell, and text replacement. Running it from the script list cycles the current foreground window in the same way as running it through its hotkey.

Other scripts remain unchanged. Unsupported action values are manifest errors rather than silently falling back to text execution.

## Hotkey model

The hotkey engine classifies bindings from their manifest rather than recognizing a special Shift action:

- A conventional chord containing a non-modifier primary key, such as `Ctrl+Alt+L`, dispatches on the primary key press using the existing behavior.
- A single modifier, such as `Shift`, `Ctrl`, `Alt`, or `Win`, is a deferred modifier gesture.
- A chord made only from distinct modifiers, such as `Ctrl+Shift` or `Alt+Shift`, is also a deferred modifier gesture. Token order does not change its meaning.
- A repeated modifier, such as the existing `Shift+Shift`, remains a double-tap sequence.

The parser produces an explicit hotkey kind and normalized modifier set. Runtime gesture handling uses those parsed values; it contains no action-specific or Shift-specific binding.

## Modifier gesture resolution

Modifier-only gestures share one small state machine so overlapping bindings are resolved consistently.

1. The first required modifier press starts a candidate and records the foreground window and start time.
2. All required modifiers must be pressed without an unrelated keyboard or mouse event.
3. The complete gesture must be released within 300 ms of the first press. Longer holds are ordinary modifier use and do not dispatch a script.
4. A completed candidate remains pending until 350 ms from the first press. Any unrelated key press, mouse button press, or foreground-window change during that interval cancels it.
5. A matching repeated-modifier binding, such as `Shift+Shift`, wins over the pending single-modifier binding. Only the double-tap script is dispatched.
6. A more specific modifier chord wins over its shorter prefix. For example, when both `Ctrl` and `Ctrl+Shift` exist, pressing the chord cancels the pending `Ctrl` action.
7. Held-key repeats never create extra candidates or dispatches.

The existing `Shift+Shift` conversion therefore keeps its current 350 ms double-tap window. `Shift+A`, `Ctrl+C`, `Alt+Tab`, mouse-assisted selection, and similar input cancel a pending modifier-only action and keep their normal behavior. Modifier events themselves continue through the hook; TextMagic does not suppress normal modifier input.

Timing is engine-wide for modifier gestures: 300 ms maximum hold and a 350 ms resolution window. These values are not added to every script manifest because per-script timing is not currently needed.

## Layout cycling

When `cycle_keyboard_layout` is dispatched:

1. Resolve and validate the foreground target window.
2. Read that window thread's current keyboard layout.
3. Read the installed layouts from `GetKeyboardLayoutList`.
4. Select the entry after the current layout, wrapping from the last entry to the first.
5. Request the selected layout for the original target window with `WM_INPUTLANGCHANGEREQUEST`.

If the target disappears, the foreground window changes before deferred dispatch, fewer than two layouts are installed, or Windows does not report the current layout in the installed list, the action performs no unsafe fallback. It records a concise status/log result and does not touch text.

## Integration boundaries

- `ScriptManifest` parses the optional built-in action and the expanded hotkey forms.
- A pure hotkey gesture helper owns timing, cancellation, and precedence decisions and is driven by keyboard/mouse hook events.
- `Application` schedules the existing message-loop timer for pending modifier gestures and dispatches the chosen script through the existing hotkey ID mapping and execution gate.
- A small pure layout helper chooses the next installed layout; `Application` performs the Win32 request.
- The text-script worker remains unchanged for scripts without a built-in action.

## Error handling and compatibility

- Duplicate normalized hotkeys continue to be rejected by registration.
- Disabled scripts do not participate in gesture resolution.
- Busy execution uses the existing script execution gate; it never queues multiple layout changes.
- Existing non-modifier hotkeys and scripts keep their current behavior.
- Existing `Shift+Shift` behavior remains available and takes precedence over the bundled single-Shift script.
- Changing the bundled script's `hotkey` is sufficient to rebind the action; no C++ change is required.

## Verification

Focused automated checks cover:

- parsing single modifiers, modifier-only chords, repeated modifiers, and conventional chords;
- normalizing modifier-only chord token order;
- single-tap dispatch only after the resolution window;
- cancellation on long hold, unrelated keyboard input, mouse input, and foreground changes;
- precedence of `Shift+Shift` over `Shift` and `Ctrl+Shift` over `Ctrl`;
- immediate dispatch of a conventional chord such as `Ctrl+Alt+L`;
- next-layout selection, wraparound, missing current layout, and one-layout no-op;
- action scripts bypassing text capture, clipboard operations, PowerShell, and replacement;
- the existing full test suite.
