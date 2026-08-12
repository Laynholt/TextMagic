# Audit Remediation Design

## Goal

Correct every confirmed defect and robustness issue from the 2026-08-12 repository audit, remove the identified dead code and speculative micro-abstractions, and simplify the repeated CMake test declarations without changing the intended UI design or custom widget behavior.

## Scope

The implementation covers:

- bounded and validated clipboard text reads;
- all-or-nothing eligibility for clipboard snapshots before temporarily replacing clipboard contents;
- correct installation checks for both global input hooks;
- safe completion delivery from detached background work;
- strict hotkey parsing;
- complete file writes and preserved Win32 errors;
- explicit `GetMessageW`, process-wait, and process-exit error handling;
- bounded script output and termination of the PowerShell process tree on timeout;
- cached fullscreen blocking decisions outside the per-key hot path;
- CMake language discovery, script deployment, warning consistency, and test-target deduplication;
- removal of unused runtime checksum generation, the single-use `ScriptInputSource` abstraction, and unused helper methods.

Custom rendering, popup controls, owner-drawn widgets, and visual layout code are outside the simplification scope unless a small call-site change is required by one of the fixes above.

## Architecture

### Clipboard safety

`ClipboardUtils::ReadText` will obtain `GlobalSize` for `CF_UNICODETEXT` and `CF_TEXT`, search for a terminator only within that allocation, and reject malformed blocks. Conversion will never construct a string from an unbounded pointer.

`ClipboardUtils::Snapshot` will record whether every enumerated clipboard format was duplicated successfully. It will expose a readiness query used by `TextBridge::CopyFromActiveControl`; the application will not send Ctrl+C and overwrite the clipboard unless the snapshot can restore the original state. Restore remains best-effort for failures that occur after the clipboard is opened, but it will report success only when every captured format was transferred successfully.

### Input hooks and fullscreen decisions

Input initialization succeeds only if both `WH_KEYBOARD_LL` and `WH_MOUSE_LL` hooks are installed. If mouse-hook installation fails, the keyboard hook is removed before returning failure so the application cannot run in a partially functional tracked-input mode.

Foreground blocking will use one cache entry containing the foreground `HWND`, blacklist generation, fullscreen-setting value, and final blocked result. Expensive DWM and process-path checks therefore run only when the foreground window or relevant configuration changes, not on every keyboard event.

### Background completion ownership

Background operations continue capturing only value objects and handles; they will not dereference `Application` after launch. Pointer-bearing completion messages will be delivered synchronously with a bounded `SendMessageTimeoutW` call after verifying that the target window still belongs to the current process and still has the expected application identity. The worker retains ownership unless the window procedure returns the handled sentinel, eliminating queued-message leaks and delivery to an unrelated reused handle.

The existing script completion path and update/import/export completion paths will use the same ownership rule. Application shutdown invalidates the dispatch target before destroying resources. Detached workers may finish independently, but they cannot access destroyed application state or leave heap payloads in an abandoned window queue.

### Script execution limits

Script stdout and stderr are each limited to 16 MiB. The runner checks temporary-file sizes before loading them and returns a localized output-limit error when either limit is exceeded. This bounds post-process memory use; the job/process timeout bounds continuing output.

PowerShell starts suspended, is assigned to a Windows Job Object configured with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, and is then resumed. Timeout or runner teardown closes the job and terminates the full child process tree. `WAIT_FAILED`, `ResumeThread`, job assignment, and `GetExitCodeProcess` failures are handled explicitly and preserve the originating Win32 error.

### Parsing, file writes, and message loop

Function-key tokens are parsed with full-consumption validation, so only `F1` through `F24` are accepted. Empty hotkey segments are rejected rather than skipped.

UTF-8 file saving uses a small `WriteAll` loop that handles partial synchronous writes, validates the BOM and payload, and captures `GetLastError` before cleanup. The same helper remains local to the file because it has one call site.

The application message loop distinguishes positive messages, `WM_QUIT`, and `GetMessageW == -1`. The runner similarly distinguishes successful waits, timeouts, abandoned/failed waits, and valid process exit-code retrieval.

### Build simplification

The language glob uses `CONFIGURE_DEPENDS`. The post-build script deployment removes the destination `scripts` directory before copying the source directory, preventing removed sample scripts from surviving incremental builds.

A CMake helper will own the repeated mechanics shared by test executables: target creation, C++17, project warning options, include directories, libraries, dependencies, and optional definitions. Individual test declarations will continue listing their exact sources and special dependencies, preserving current linkage while reducing repeated boilerplate.

### Dead-code removal

The application will stop generating a local `SHA256SUMS.txt` on startup. Release checksum verification remains unchanged: the updater still downloads the release-provided checksum file and verifies the downloaded executable. The production checksum writer is removed; tests that need checksum fixtures will create them using test-only code.

`ScriptInputSource::Choose` is inlined as direct source-selection control flow in the sole production caller. Its header, dedicated test executable, and test file are removed. `ScriptExecutionGate::IsReserved` and `HotkeyDispatch::ShouldTrackInput` are removed because they have no callers.

## Error Handling

- Malformed clipboard data is treated as unavailable text without reading outside its allocation.
- An incomplete clipboard snapshot prevents selection-copy mode instead of risking clipboard data loss.
- Partial hook installation fails application initialization with the existing hook error path.
- Completion delivery failure destroys the result on the worker thread and performs no UI access.
- Script output-limit and process-control failures return localized execution errors and clean temporary files and handles.
- A message-loop failure returns a nonzero process result and records a diagnostic when logging is still available.
- Build changes do not alter runtime behavior beyond removing stale deployed scripts and the unused runtime checksum artifact.

## Testing

Regression tests will be added before the corresponding production changes and observed failing for the intended reason.

- Clipboard tests create unterminated Unicode and ANSI clipboard blocks and require `ReadText` to reject them. Snapshot tests exercise the completeness decision where practical without relying on another process.
- Manifest tests reject `F1suffix`, leading/trailing separators, and doubled separators.
- Script-runner tests cover the 16 MiB output boundary and a child process that must not survive timeout when reliable process observation is available.
- Pure decision helpers introduced only where necessary for Win32 failure injection receive focused unit tests; no broad mocking framework is added.
- Existing tests cover unchanged behavior after each red-green cycle.
- Final verification consists of a clean Release build with `/W4`, all CTest targets, `git diff --check`, and a clean review of the resulting diff.

## Compatibility and Constraints

- C++17 and the existing Windows/MSVC and MinGW build paths remain supported.
- No third-party dependencies are added.
- Existing `.tmscript` format and intended hotkeys remain compatible; only previously malformed forms are rejected.
- The 16 MiB limit applies independently to stdout and stderr.
- No visual or interaction redesign is included.
