# Audit Remediation Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Fix every confirmed correctness and robustness issue from the repository audit, remove the agreed dead code, and simplify the repeated build declarations without changing custom UI behavior.

**Architecture:** Keep Win32 ownership explicit at existing module boundaries. Extract only small pure decision helpers where a failing regression test cannot otherwise reach a platform error path; do not add frameworks, services, or third-party dependencies.

**Tech Stack:** C++17, Win32, CMake, CTest, MSVC/MinGW.

---

## Global Constraints

- Work from `docs/superpowers/specs/2026-08-12-audit-remediation-design.md`.
- Follow strict RED -> GREEN -> REFACTOR for every behavior change and show the failing test before production edits.
- Keep custom controls, rendering, popup behavior, and visual layout unchanged.
- Preserve MSVC and MinGW compatibility and add no dependency.
- Limit stdout and stderr independently to exactly `16 * 1024 * 1024` bytes.
- Run the focused test after each edit and the full Release suite before every task commit.
- Commit each task separately; never mix unrelated cleanup into a behavioral commit.

### Task 1: Make clipboard reads and snapshots loss-safe

**Files:**
- Modify: `src/core/common/ClipboardUtils.h`
- Modify: `src/core/common/ClipboardUtils.cpp`
- Modify: `src/core/text/TextBridge.cpp`
- Modify: `tests/ClipboardUtilsTests.cpp`

**Step 1: Write failing regression tests**

Add clipboard allocations without terminators for both `CF_UNICODETEXT` and `CF_TEXT`; assert `ClipboardUtils::ReadText` returns `false`. Add snapshot assertions for `IsComplete()` and for `Restore()` reporting failure unless every captured format transfers.

```cpp
ClipboardUtils::Snapshot snapshot;
if (!Check(snapshot.IsComplete(), "fully duplicated snapshot is restorable")) return 1;
// Allocate two wchar_t values without L'\\0', publish as CF_UNICODETEXT.
if (!Check(!ClipboardUtils::ReadText(nullptr, &text), "unterminated Unicode is rejected")) return 1;
```

**Step 2: Run RED**

Run: `rtk cmake --build build --config Release --target TextMagicClipboardUtilsTests --parallel`
Run: `rtk ctest --test-dir build -C Release -R TextMagicClipboardUtilsTests --output-on-failure`
Expected: compile failure because `IsComplete` does not exist, then behavioral failure for unterminated data after adding the declaration only.

**Step 3: Implement the minimum fix**

Bound both format reads by `GlobalSize`, locate the terminator with bounded iteration, and reject malformed blocks. Track snapshot completeness while enumerating formats.

```cpp
bool Snapshot::IsComplete() const noexcept { return m_complete; }

const SIZE_T bytes = GlobalSize(data);
const auto* end = static_cast<const wchar_t*>(raw) + bytes / sizeof(wchar_t);
const auto* terminator = std::find(static_cast<const wchar_t*>(raw), end, L'\0');
if (terminator == end) return false;
```

In `TextBridge::CopyFromActiveControl`, return without issuing Ctrl+C unless the pre-copy snapshot is complete. Make `Restore()` return `true` only when every captured format was successfully transferred.

**Step 4: Run GREEN and commit**

Run the focused commands above, then `rtk ctest --test-dir build -C Release --output-on-failure`.
Commit: `fix: make clipboard access loss-safe`

### Task 2: Enforce strict hotkey syntax and delete adjacent dead helpers

**Files:**
- Modify: `src/core/scripts/ScriptManifest.cpp`
- Modify: `tests/ScriptManifestTests.cpp`
- Modify: `src/app/ScriptExecutionGate.h`
- Modify: `tests/ScriptExecutionGateTests.cpp`

**Step 1: Write failing parser tests**

Reject `F1suffix`, `Ctrl++A`, `+A`, `A+`, and whitespace-only segments while retaining valid `F1` through `F24`.

```cpp
for (const auto* invalid : {L"F1suffix", L"Ctrl++A", L"+A", L"A+"}) {
    if (!Check(!ScriptManifest::ParseHotkey(invalid, &hotkey, &error), "invalid hotkey rejected")) return 1;
}
```

**Step 2: Run RED**

Run: `rtk cmake --build build --config Release --target TextMagicScriptManifestTests --parallel`
Run: `rtk ctest --test-dir build -C Release -R TextMagicScriptManifestTests --output-on-failure`
Expected: malformed hotkeys are accepted.

**Step 3: Implement and clean dead code**

Require every `+`-separated token to be non-empty and function-key parsing to consume the full token.

```cpp
wchar_t* end = nullptr;
const long number = std::wcstol(token.c_str() + 1, &end, 10);
if (end == token.c_str() + 1 || *end != L'\0' || number < 1 || number > 24) return false;
```

Delete unused `ScriptExecutionGate::IsReserved()` and `HotkeyDispatch::ShouldTrackInput()`, plus any tests that exist only for those unused functions.

**Step 4: Run GREEN and commit**

Run the focused commands and full CTest suite.
Commit: `fix: reject malformed hotkeys`

### Task 3: Make UTF-8 saves and the message loop report real failures

**Files:**
- Modify: `src/app/AppUiHelpers.cpp`
- Modify: `src/app/Application.cpp`
- Modify or add focused tests only if a pure helper is required: `tests/`

**Step 1: Introduce a testable decision and failing test**

Extract only the message-result classification needed to test `GetMessageW`: positive => dispatch, zero => quit, `-1` => error. Add a focused test to the smallest existing application helper test target.

```cpp
enum class MessageReadResult { Dispatch, Quit, Error };
constexpr MessageReadResult ClassifyMessageRead(BOOL value) noexcept {
    return value > 0 ? MessageReadResult::Dispatch :
           value == 0 ? MessageReadResult::Quit : MessageReadResult::Error;
}
```

Run the selected target and observe RED before adding the production helper.

**Step 2: Implement complete writes**

Add a file-local `WriteAll(HANDLE, const void*, size_t, DWORD*)` loop in `AppUiHelpers.cpp`. Treat a successful zero-byte write as failure, chunk sizes to `DWORD`, and save `GetLastError()` before `CloseHandle`.

```cpp
while (remaining != 0) {
    DWORD written = 0;
    if (!WriteFile(file, cursor, chunk, &written, nullptr) || written == 0) return false;
    cursor += written;
    remaining -= written;
}
```

Use it for both BOM and payload. Update the main loop to return nonzero and log a diagnostic for `GetMessageW == -1`.

**Step 3: Run GREEN and commit**

Run the focused target, full CTest suite, and Release app build.
Commit: `fix: handle write and message loop failures`

### Task 4: Harden script process control and bound output

**Files:**
- Modify: `src/core/scripts/ScriptRunner.h`
- Modify: `src/core/scripts/ScriptRunner.cpp`
- Modify: `tests/ScriptRunnerTests.cpp`
- Modify localization resources only if the existing generic error cannot express the limit: `src/app/Localization.*` and language resources.

**Step 1: Write failing tests**

Add tests for output at/below the 16 MiB boundary, output above it, and timeout of a script that starts a long-lived child. Record the child PID and assert that it no longer exists after timeout.

```cpp
constexpr std::uint64_t kMaxCapturedStreamBytes = 16ull * 1024ull * 1024ull;
// Exactly the limit succeeds; limit + 1 returns OutputLimitExceeded.
```

**Step 2: Run RED**

Run: `rtk cmake --build build --config Release --target TextMagicScriptRunnerTests --parallel`
Run: `rtk ctest --test-dir build -C Release -R TextMagicScriptRunnerTests --output-on-failure`
Expected: oversized output is loaded and/or the spawned child remains alive.

**Step 3: Implement job ownership and explicit errors**

Create PowerShell suspended, create a Job Object, set `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, assign the process, and resume only after successful assignment. On timeout, close/terminate the job and wait for the process. Treat `WAIT_FAILED`, unexpected wait values, `ResumeThread == DWORD(-1)`, job API failures, and `GetExitCodeProcess == FALSE` as errors while preserving the first Win32 error.

Before reading either temp output file, query its size and independently reject stdout or stderr above `kMaxCapturedStreamBytes`.

```cpp
JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
CreateProcessW(..., CREATE_SUSPENDED | CREATE_NO_WINDOW, ...);
AssignProcessToJobObject(job, pi.hProcess);
ResumeThread(pi.hThread);
```

**Step 4: Run GREEN and commit**

Run the focused test repeatedly twice to catch process-cleanup races, then full CTest.
Commit: `fix: contain script processes and output`

### Task 5: Make hooks and foreground blocking state coherent

**Files:**
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Modify: `src/app/FullscreenUtils.h` only if a pure cache-key helper belongs there
- Modify: `tests/FullscreenUtilsTests.cpp` or add a small application-state helper test

**Step 1: Write failing pure-state tests**

Test that the blocking cache invalidates when any of foreground `HWND`, blacklist generation, or fullscreen setting changes, and remains reusable only when all three match.

```cpp
if (!Check(!cache.Matches(hwnd, generation + 1, fullscreen), "blacklist change invalidates cache")) return 1;
if (!Check(!cache.Matches(hwnd, generation, !fullscreen), "setting change invalidates cache")) return 1;
```

Run the focused target and observe RED.

**Step 2: Implement hook rollback and cached decision**

Require both low-level hooks. If mouse installation fails, unhook and null the keyboard hook before returning `false`. Store one cache record `{HWND, blacklistGeneration, fullscreenSetting, blocked}` and move DWM/process-path work behind a cache miss. Increment/expose a generation whenever the blacklist changes.

**Step 3: Run GREEN and commit**

Run focused tests and full CTest.
Commit: `fix: keep input hook state coherent`

### Task 6: Give background completion payloads bounded synchronous ownership

**Files:**
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Modify existing tests or add a narrow helper test under `tests/`

**Step 1: Write failing ownership-decision tests**

Extract a small, platform-neutral result classifier for synchronous delivery: handled sentinel transfers ownership; timeout/failure/wrong identity retains it. Test all cases before wiring Win32 calls.

```cpp
constexpr LRESULT kCompletionHandled = 1;
constexpr bool CompletionOwnershipTransferred(bool delivered, DWORD_PTR result) {
    return delivered && result == static_cast<DWORD_PTR>(kCompletionHandled);
}
```

**Step 2: Run RED**

Build/run the chosen helper test target; expected compile failure until the helper exists.

**Step 3: Replace pointer-bearing queued delivery**

Before delivery verify `IsWindow`, the window process ID equals `GetCurrentProcessId`, and `GWLP_USERDATA` still equals the captured `Application*` identity. Use bounded `SendMessageTimeoutW` with `SMTO_ABORTIFHUNG | SMTO_BLOCK`. Each handler that accepts a payload must return `kCompletionHandled`. Keep the `std::unique_ptr` in the worker and call `release()` only after that sentinel is returned.

```cpp
DWORD_PTR handled = 0;
const LRESULT delivered = SendMessageTimeoutW(hwnd, message,
    reinterpret_cast<WPARAM>(payload.get()), 0,
    SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &handled);
if (CompletionOwnershipTransferred(delivered != 0, handled)) payload.release();
```

At shutdown invalidate the dispatch target before destroying UI/application resources. Detached lambdas capture only values/handles, never a reference or dereferenceable owner.

**Step 4: Run GREEN and commit**

Run the focused target and full CTest suite.
Commit: `fix: make background completion ownership explicit`

### Task 7: Remove startup-only abstractions and checksum generation

**Files:**
- Modify: `src/app/Application.cpp`
- Delete: `src/app/ScriptInputSource.h`
- Delete: `tests/ScriptInputSourceTests.cpp`
- Modify: `src/core/update/UpdateService.h`
- Modify: `src/core/update/UpdateService.cpp`
- Modify: `tests/UpdateServiceTests.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Move fixture creation into the test and observe RED**

Change `UpdateServiceTests.cpp` to create its `SHA256SUMS.txt` fixture locally with known text, then remove its call to the production checksum writer. Run the test before deleting production code to prove the fixture is independent.

**Step 2: Delete production dead code**

Remove startup generation of local `SHA256SUMS.txt` and the now-unused production checksum writer. Keep release checksum download and executable verification intact. Inline `ScriptInputSource::Choose` at its sole caller as direct conditional selection, then delete the header, dedicated test, and CMake test target.

```cpp
const std::wstring input = useSelection
    ? m_textBridge->ReadSelection()
    : m_inputBuffer.GetText();
```

**Step 3: Run GREEN and commit**

Run: `rtk cmake --build build --config Release --target TextMagicUpdateServiceTests --parallel`
Run: `rtk ctest --test-dir build -C Release -R "TextMagicUpdateServiceTests|TextMagicScriptInputSourceTests" --output-on-failure`
Expected: update tests pass and the deleted test is no longer registered. Then run full CTest.
Commit: `refactor: remove unused startup helpers`

### Task 8: Simplify and harden CMake build declarations

**Files:**
- Modify: `CMakeLists.txt`

**Step 1: Capture current test inventory**

Run: `rtk ctest --test-dir build -C Release -N`
Record the expected count after Task 7 (17 tests).

**Step 2: Implement build simplification**

Add `CONFIGURE_DEPENDS` to the language glob. Before the post-build `copy_directory`, remove `$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts`. Define a helper that applies repeated test setup and project warning flags.

```cmake
function(textmagic_add_test target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    textmagic_apply_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()
```

Keep each target's explicit sources, libraries, definitions, and dependencies. Do not use a generator abstraction that hides special linkage.

**Step 3: Prove stale scripts are removed**

Configure and build once, create a disposable marker only inside `build/Release/scripts`, rebuild the app target, and assert the marker is gone while source scripts remain copied. Do not modify source scripts.

**Step 4: Verify and commit**

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Release --clean-first --parallel
rtk ctest --test-dir build -C Release --output-on-failure
rtk proxy git diff --check
```

Expected: clean Release build with project warnings enabled for every test, 17/17 tests pass, stale marker removed.
Commit: `build: simplify tests and refresh deployed scripts`

## Final Verification

Run from a clean build directory or with `--clean-first`:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Release --clean-first --parallel
rtk ctest --test-dir build -C Release --output-on-failure
rtk proxy git diff --check
rtk proxy git status --short --branch
```

Request a whole-branch code review against the plan and design spec. Resolve every correctness issue, rerun the complete verification, then fast-forward the reviewed branch into `master` as authorized by the user.
