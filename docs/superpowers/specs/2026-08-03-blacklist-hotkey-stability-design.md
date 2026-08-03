# Application Blacklist And Hotkey Stability Design

## Goal

Make script hotkeys safe under repeated input, eliminate log-view rendering artifacts, and replace the fullscreen-only exclusions window with a persistent application blacklist. In blocked applications TextMagic must neither capture typed text nor consume script hotkeys; the original key combination must reach the foreground application unchanged.

## Scope

This change includes:

- one uniform low-level hook path for every script hotkey;
- an early single-execution gate with cooldown and spam suppression;
- a persistent blacklist of executable paths;
- a redesigned blacklist window with a styled checkbox and application management;
- selection from visible running applications and from a modern `.exe` file dialog;
- a RichEdit-based log viewer while retaining the session log file as the source of truth;
- exception containment for detached script workers.

Per-script blacklists, wildcard rules, filename-only rules, UWP package identity, background-process selection, and automatic migration when an executable moves are outside this scope.

## Findings

The persisted session log contains correct, non-overlapping lines. The visible corruption is isolated to the themed standard `EDIT` used by the log window: it receives incremental `EM_REPLACESEL` updates while the Explorer theme and parent-provided background brush compete over erasure.

Script execution currently has a late guard in `ExecuteScript`, after Windows or the hook has already dispatched the hotkey. This still lets repeated events enter the UI queue. Modal error handling also runs a nested message loop, so the execution reservation must remain held until result handling has completely finished.

`RegisterHotKey` cannot satisfy blacklist pass-through because Windows consumes a registered combination before TextMagic can decide to ignore it. All script hotkeys therefore need the existing low-level-hook route.

## Hotkey Architecture

### Unified dispatch

Every enabled script is represented in the in-memory hook hotkey table. TextMagic no longer calls `RegisterHotKey` for script shortcuts. Existing matching rules, duplicate detection, modifier-double-tap handling, key-release rearming, and injected-input filtering remain in effect.

The hook performs no file I/O. It reads the already loaded blacklist and resolves a process path only when the foreground window changes or the blacklist generation changes. A small foreground cache stores:

- the last foreground `HWND`;
- the last blacklist generation;
- whether TextMagic is blocked for that foreground window.

### Dispatch sequence

For each physical keyboard event:

1. Resolve or reuse the cached foreground-blocked state.
2. If blocked by the executable blacklist or the fullscreen option, skip input-buffer tracking and call `CallNextHookEx` for every event.
3. Otherwise test whether the event matches a script hotkey before modifying the input buffer.
4. A non-hotkey event follows the normal input-buffer path and is passed onward.
5. A matched hotkey tries to reserve the execution gate before posting a message.
6. If the gate is already busy or inside its cooldown, consume the matched hotkey but do not post a message, create a worker, change status, or append an error.
7. If reservation succeeds, post one dispatch message and consume the hotkey.
8. If posting fails, release the reservation immediately.

The gate is released only after `WM_SCRIPT_EXECUTION_COMPLETE` has applied or reported the result and any modal error window has closed. The cooldown begins at that release point. Invalid script identifiers and synchronous launch failures also release the reservation.

Manual execution from the TextMagic window uses the same single-execution state but does not involve global hotkey suppression.

## Worker Error Containment

The complete detached worker body is wrapped in `try`/`catch`. Standard exceptions are converted to a normal failed `ScriptExecutionTaskResult`; unknown exceptions receive a localized generic error. No exception may escape the worker entry point.

Failure to create the worker thread is handled synchronously: TextMagic releases the execution gate, writes one log entry, and shows one error status. A no-text result is non-modal and produces one status/log update. Other existing execution errors may remain modal, but the gate stays reserved until their dialog closes, so hotkey spam cannot open nested executions.

## Blacklist Model And Persistence

`ApplicationBlacklist` owns normalized full executable paths in memory. Matching is case-insensitive through ordinal Windows path comparison. Empty entries and duplicate paths are ignored.

The file is named `TextMagic.blacklist` and lives next to the executable. It is UTF-8, optional BOM, with one full executable path per line. The simple line format is intentionally human-editable and does not overload `TextMagic.settings.ini`, whose fullscreen flag remains unchanged.

Loading rules:

- a missing file means an empty blacklist;
- blank or invalid lines are ignored;
- a read error is logged once and does not prevent startup.

Saving writes the complete set to a temporary sibling file, flushes and closes it, then replaces `TextMagic.blacklist`. A failed save leaves the previous file intact and is reported in the blacklist window.

Foreground matching uses `GetForegroundWindow`, `GetWindowThreadProcessId`, `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`, and `QueryFullProcessImageNameW`. If the path cannot be obtained, TextMagic fails open and does not block or consume the hotkey.

## Blacklist Window

The localized menu item becomes `Application blacklist...` / `Чёрный список приложений...`. The resizable window starts near 760 by 520 pixels.

Layout:

- a styled fullscreen checkbox at the top;
- a report-style application list with `Application` / `Приложение` and `Path` / `Путь` columns;
- `Running applications...` / `Из запущенных...`;
- `Add .exe...` / `Добавить .exe...`;
- `Remove` / `Удалить`;
- `Close` / `Закрыть`.

Rows show the executable filename and its full stored path. Add and remove operations persist immediately. The Remove button is disabled without a selection; the Delete key invokes the same operation.

### Styled checkbox

The checkbox keeps a native `BUTTON` with automatic checkbox behavior, focus, keyboard input, and accessibility. A window subclass replaces only its painting. The indicator, hover, pressed, checked, disabled, text, and focus states adapt the drawing approach from `win32-custom-widgets/src/BooleanControl.cpp` to TextMagic's existing palette and `UiRenderer` primitives. The external widget library is not added as a dependency.

### Running application selector

`Running applications...` opens a modal, dark, multi-select list. It enumerates visible top-level windows, ignores untitled/inaccessible entries, resolves their executable paths, and deduplicates by normalized full path. Each row shows executable filename, window title, and path. `Add selected` adds all selected entries and closes the selector; `Cancel` changes nothing.

### Executable picker

`Add .exe...` uses `IFileOpenDialog` with `FOS_FORCEFILESYSTEM`, `FOS_FILEMUSTEXIST`, `FOS_ALLOWMULTISELECT`, and an executable filter. It permits selecting one or more `.exe` files. Returned filesystem paths go through the same normalization and deduplication path as running applications.

## Log Viewer

The session log file remains authoritative and is still truncated once after the single-instance check during application startup.

The logs window uses the system `RICHEDIT50W` control from `Msftedit.dll`:

- read-only multiline text with vertical scrolling;
- monospaced font;
- explicit `EM_SETBKGNDCOLOR` and text formatting in the TextMagic palette;
- explicit left and right margins;
- no Explorer theme on the text surface.

Opening the window reads the complete current session file once. Each successful `LogFile::Append` then appends only the new formatted line to the open RichEdit. Clear reloads the empty-state text. Save As reads the file rather than relying on a second growing in-memory log copy.

If RichEdit cannot be loaded, TextMagic falls back to a plain read-only `EDIT` without the Explorer theme and forces a full invalidation after append. This fallback preserves functionality, although RichEdit is the normal Windows 10 path.

## Error Handling

- Blacklisted or fullscreen foreground: complete pass-through and no input capture.
- Busy/cooldown hotkey: consumed silently with no queued work.
- Foreground path unavailable: fail open.
- Blacklist load failure: start with entries successfully parsed and log once.
- Blacklist save failure: keep the old file and show a localized error.
- Running process becomes unavailable during enumeration: skip it.
- No source text: non-modal status and one log entry.
- Script or worker exception: failed result; process remains alive.
- RichEdit unavailable: functional plain-EDIT fallback.

## Testing

Automated tests cover:

- execution reservation, busy suppression, cooldown, and release;
- allowed versus blocked dispatch decisions;
- blocked input-buffer bypass and pass-through decisions;
- blacklist UTF-8 round trip, missing file, blank lines, case-insensitive deduplication, and replacement-safe saving;
- normalized full-path matching;
- visible-running-application deduplication through a pure model helper;
- existing log file append, clear, and session reset behavior;
- worker exception-to-result conversion where it can be isolated from Win32 UI code.

The complete Release build and all CTest targets must pass. Manual smoke testing covers:

- rapid hotkey spam with and without source text in Sublime Text and Notepad;
- no nested dialogs, hang, or process termination;
- blacklist pass-through to the foreground application;
- fullscreen pass-through;
- adding multiple running applications;
- adding `.exe` files through `IFileOpenDialog`;
- removal and persistence across restart;
- log append, resize, scroll, clear, close, and reopen without glyph overlap.
