# TextMagic

[Русский](README.md) | [English](README.en.md)

TextMagic is a Windows application that processes text in the active input field using global hotkeys. Its functionality can be extended with `.tmscript` files, so scripts can be added or changed without recompiling the application.

## Included scripts

| Script | Hotkey | Purpose |
| --- | --- | --- |
| Layout Auto QWERTY | `Shift+Shift` | Fixes text typed with the wrong Russian or English QWERTY layout, preserves letter case, and switches the input language to match the result. |
| Next Keyboard Layout | `Shift` | Switches the active application to the next installed keyboard layout. |
| Uppercase | `Ctrl+Alt+U` | Converts text to uppercase. |
| Lowercase | `Ctrl+Alt+L` | Converts text to lowercase. |

Hotkeys are not hard-coded in the application. They can be changed in the corresponding `.tmscript` files.

## How text is selected

When a script is triggered, TextMagic uses:

1. the explicitly selected text;
2. if nothing is selected, the last typed word or all tracked text, depending on the selected mode.

Without an explicit selection, TextMagic does not read the entire contents of the active input field.

## `.tmscript` scripts

Scripts are loaded from the `scripts` directory next to `TextMagic.exe`. Each script is stored in a separate `.tmscript` file containing metadata followed by a PowerShell script body.

Example:

```ini
name=Uppercase
description=Converts selected or recently typed text to uppercase
hotkey=Ctrl+Alt+U
enabled=true
---
[Console]::InputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$inputText = [Console]::In.ReadToEnd()
[Console]::Out.Write($inputText.ToUpperInvariant())
```

Manifest fields:

- `name` — the name displayed in the application;
- `description` — a short description;
- `hotkey` — a hotkey or key combination;
- `enabled` — the script state (`true` or `false`);
- the `---` line separates the metadata from the PowerShell script body.

## Execution protocol

1. TextMagic sends the source text to the script through `stdin` using UTF-8.
2. The script returns the processed text through `stdout` using UTF-8.
3. A successful script must exit with code `0`.

## License

This project is available under the [MIT License](LICENSE).
