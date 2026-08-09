# Layout Auto QWERTY: Case Preservation and Target Layout

## Goal

`layout_auto_qwerty.tmscript` must preserve the input letter case while converting between English and Russian QWERTY layouts. After an in-place conversion triggered by the script hotkey, the target application's keyboard layout must match the converted text.

Expected examples:

- `ghbdtn` -> `привет`
- `Ghbdtn` -> `Привет`
- `GHBDTN` -> `ПРИВЕТ`
- The equivalent Russian-to-English conversions preserve the same per-character case.

## Scope

- Add an optional script manifest setting: `output_layout=auto`.
- Enable it only for the bundled `layout_auto_qwerty.tmscript`.
- Switch layout only after a successful in-place replacement in the original target window.
- Do not switch layout when the script is run manually in clipboard mode.
- Preserve unrelated user files and avoid changing other scripts' behavior.

## Design

The manifest parser stores whether a script requests automatic output-layout selection. The execution result carries this flag back to the UI thread together with the original target window.

After replacement succeeds, the application inspects the converted output. More Cyrillic than Latin letters selects the Russian layout; more Latin than Cyrillic selects the English layout. A tie or text containing neither alphabet leaves the layout unchanged. The application sends `WM_INPUTLANGCHANGEREQUEST` to the original target window so the layout change applies to the application receiving subsequent typing.

Case conversion remains the responsibility of `layout_auto_qwerty.tmscript`. Its lower- and uppercase mappings are tested through the real manifest and script runner rather than duplicated in production C++.

## Failure Handling

- A missing requested RU or EN keyboard layout leaves the current layout unchanged.
- A failed text replacement does not change the layout.
- A failed layout-change request does not turn an otherwise successful text replacement into a script failure.
- Scripts without `output_layout=auto` retain existing behavior.

## Tests

- Run the bundled script against lowercase, title-case, and uppercase English input and assert the expected Russian output.
- Run the equivalent Russian inputs and assert English output with matching case.
- Test output-language detection for Cyrillic, Latin, punctuation-only, and mixed text.
- Test manifest parsing with the setting present, absent, and unsupported values.
- Run the complete Release build and CTest suite.
