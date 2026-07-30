# Localization and Simplification Design

## Goal

Remove the runtime duplication of bundled language files while preserving
editable user translations, then apply the approved non-UI simplifications
from the Ponytail audit without changing application behavior.

## Localization Sources

- `lang/*.ini` files are manually maintained translation sources.
- CMake embeds every source `.ini` file into `EmbeddedLanguages.h`.
- The build does not translate, clone, or otherwise generate one language
  from another.
- Russian remains the default application language.

## Runtime Loading

1. Load all embedded language maps from the executable.
2. If the external `lang` directory exists, load each `*.ini` file found
   there.
3. For an external language whose code matches an embedded language, merge
   its keys over the embedded map. Missing external keys retain their
   embedded values.
4. An external file with a new language code adds that language and starts
   from the existing fallback map before applying its own keys.
5. The available-language list is the union of embedded and external
   language codes.

The fallback order remains:

1. the requested embedded language;
2. embedded Russian;
3. embedded English;
4. the first available embedded language;
5. an empty string if no translation exists.

## Runtime Filesystem Behavior

- The application does not create the external `lang` directory.
- The application does not write embedded `ru.ini`, `en.ini`, or any other
  embedded translation to disk.
- A user-created external `ru.ini` or `en.ini` overrides the corresponding
  embedded translation.
- Removing an external file restores the embedded translation on the next
  application start.

## Approved Simplifications

The implementation also applies the non-UI findings from the Ponytail audit:

- remove the enum-based localization API and use the existing string-key API;
- reuse `EncodingUtils::Utf8ToWide` in `ScriptRunner::BytesToWide`;
- replace the hand-written Base64 encoder with Windows
  `CryptBinaryToStringW`;
- remove the redundant CMake `HEADERS` list while keeping
  `EmbeddedLanguages.h` as a generated target source;
- keep one shared `BuildDialogFilter` implementation;
- replace `ReadFileToString` Win32 file handling with `std::ifstream`;
- remove direct clipboard delegators and call `ClipboardUtils` directly;
- store the stateless `UpdateService` by value;
- remove post-build copying of the `lang` directory;
- remove redundant `WIN32` guards from this Windows-only project;
- remove the one-call `Application::ImportScriptFile` delegator;
- replace `PowerShellUtils::GetExecutableName()` with the fixed
  `powershell.exe` literal;
- remove the unused `TextBridge.cpp` word-separator helper;
- remove the never-overridden `WideToUtf8` fallback parameter while
  preserving its current fallback behavior.

Custom widgets, rendering, styled dialogs, and context-menu implementations
are explicitly out of scope.

## Error Handling

- Missing external directories and files are normal and produce no error.
- An unreadable or invalid external language file is ignored; the embedded
  language remains usable.
- A partial external file cannot erase missing embedded keys.
- Existing fallback behavior remains responsible for absent translation
  keys.

## Verification

Add a localization regression test that proves:

- initialization with no external directory creates no directory or language
  files;
- an external translation overrides the matching embedded key;
- an omitted external key still resolves to the embedded value;
- an external language code appears in the available-language list.

Run the complete CMake configure, build, and CTest suite after the
refactoring. The existing custom UI behavior must remain untouched.

