# Audit Findings Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix every P1-P3 issue from the 2026-08-09 audit without a broad `Application.cpp` refactor.

**Architecture:** Keep the existing Win32 design. Replace pipe-based child I/O with temporary files so process timeout controls execution, preserve the complete OLE clipboard object, require a trusted Authenticode signature before self-update, add a small pure helper for popup keyboard navigation, and reuse existing filesystem/Win32 facilities for the remaining fixes.

**Tech Stack:** C++17, Win32, WinHTTP, WinTrust, CMake/CTest, MSVC.

## Global Constraints

- No new third-party dependencies.
- Preserve existing public behavior except where the audit identified it as broken.
- Add one focused regression check for each non-trivial behavior change.
- Keep the existing 30-second production script timeout, with a test-only-callable timeout argument.

---

### Task 1: Script execution timeout and stderr semantics

**Files:**
- Modify: `src/core/scripts/ScriptRunner.h`
- Modify: `src/core/scripts/ScriptRunner.cpp`
- Test: `tests/ScriptRunnerTests.cpp`

**Interfaces:**
- Consumes: `ScriptRunner::Execute` and `ExecutePowerShellScript`.
- Produces: the same methods with an optional `DWORD timeoutMs = 30000` argument.

- [ ] **Step 1: Write failing integration checks**

```cpp
Check(runner.ExecutePowerShellScript(
    L"[Console]::Error.Write('warning')\n[Console]::Out.Write('ok')",
    L"", &output, &error),
    "stderr does not fail a zero-exit script");

const auto started = std::chrono::steady_clock::now();
Check(!runner.ExecutePowerShellScript(
    L"Start-Sleep -Seconds 5\n[Console]::Out.Write('late')",
    L"", &output, &error, 100),
    "timeout stops a child that keeps stdout open");
Check(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
    "timeout is enforced before stdout reaches EOF");
```

- [ ] **Step 2: Run `TextMagicScriptRunnerTests` and verify failure**

Run: `cmake --build build --config Release --target TextMagicScriptRunnerTests && ctest --test-dir build -C Release -R TextMagicScriptRunnerTests --output-on-failure`

Expected: stderr check fails and the timeout check takes about five seconds or does not compile until the timeout parameter exists.

- [ ] **Step 3: Replace child pipes with inheritable temporary files**

```cpp
// Write stdin to a temporary file, rewind it, start the child, wait with timeout,
// then rewind/read stdout and stderr after the child exits.
const DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
```

Return failure only for a non-zero exit code; include stderr in the diagnostic for that case.

- [ ] **Step 4: Run the focused test and all tests**

Run the command from Step 2, then `ctest --test-dir build -C Release --output-on-failure`.

- [ ] **Step 5: Commit**

```bash
git add src/core/scripts/ScriptRunner.h src/core/scripts/ScriptRunner.cpp tests/ScriptRunnerTests.cpp
git commit -m "fix: make script execution timeout reliable"
```

### Task 2: Clipboard fidelity and deterministic paste completion

**Files:**
- Modify: `src/core/common/ClipboardUtils.cpp`
- Modify: `src/core/text/TextBridge.cpp`
- Modify: `CMakeLists.txt`
- Modify: `.gitignore`
- Create: `tests/ClipboardUtilsTests.cpp`

**Interfaces:**
- Consumes: `ClipboardUtils::Snapshot` and `TextBridge::PasteIntoActiveControl`.
- Produces: full-format OLE restore and synchronous `WM_PASTE` delivery to the focused control.

- [ ] **Step 1: Write a failing clipboard-format regression test**

```cpp
const UINT customFormat = RegisterClipboardFormatW(L"TextMagic.ClipboardUtilsTests");
// Put CF_UNICODETEXT and customFormat on the clipboard, construct Snapshot,
// replace the clipboard with temporary text, call Restore(), then assert both
// formats are available again.
```

- [ ] **Step 2: Add the test target and verify failure**

Run: `cmake --build build --config Release --target TextMagicClipboardUtilsTests && ctest --test-dir build -C Release -R TextMagicClipboardUtilsTests --output-on-failure`

Expected: custom clipboard format is missing after restore.

- [ ] **Step 3: Prefer the captured IDataObject during restore**

```cpp
if (m_dataObject && SUCCEEDED(OleSetClipboard(m_dataObject))
    && SUCCEEDED(OleFlushClipboard())) {
    m_restored = true;
    return;
}
```

Keep text/empty restoration only as fallback.

- [ ] **Step 4: Replace the fixed sleep with synchronous paste**

Resolve the focused HWND with `GetGUIThreadInfo`, write the temporary clipboard text, and use `SendMessageTimeoutW(..., WM_PASTE, ..., SMTO_ABORTIFHUNG, 1000, ...)`. Restore the snapshot only after the paste handler returns.

- [ ] **Step 5: Run focused and full tests, then commit**

```bash
git add .gitignore CMakeLists.txt src/core/common/ClipboardUtils.cpp src/core/text/TextBridge.cpp tests/ClipboardUtilsTests.cpp
git commit -m "fix: preserve clipboard formats during paste"
```

### Task 3: Trusted and recoverable self-update

**Files:**
- Modify: `src/core/update/UpdateService.h`
- Modify: `src/core/update/UpdateService.cpp`
- Modify: `CMakeLists.txt`
- Modify: `.gitignore`
- Create: `tests/UpdateServiceTests.cpp`

**Interfaces:**
- Produces: `UpdateService::VerifyExecutableTrust(const std::wstring&, std::wstring&)`.
- `DownloadReleaseExecutable` calls verification before reporting success.

- [ ] **Step 1: Write failing trust checks**

```cpp
Check(!UpdateService::VerifyExecutableTrust(textFile, error),
      "plain text is rejected as an update executable");
Check(UpdateService::VerifyExecutableTrust(systemKernel32, error),
      "a trusted Windows binary is accepted");
```

- [ ] **Step 2: Add the target and verify it fails to compile**

Run: `cmake --build build --config Release --target TextMagicUpdateServiceTests`.

- [ ] **Step 3: Implement WinVerifyTrust validation**

Use `WINTRUST_ACTION_GENERIC_VERIFY_V2`, `WTD_UI_NONE`, and close the WinTrust state. Delete the downloaded file when validation fails. Link `wintrust`.

- [ ] **Step 4: Make replacement recoverable**

Generate a PowerShell updater that moves the current exe to `.bak`, moves the verified download into place, starts it, and restores/restarts the backup in `catch`. Remove the backup only after `Start-Process` succeeds.

- [ ] **Step 5: Run focused/full tests and commit**

```bash
git add .gitignore CMakeLists.txt src/core/update/UpdateService.h src/core/update/UpdateService.cpp tests/UpdateServiceTests.cpp
git commit -m "fix: verify and recover self-updates"
```

### Task 4: Manifest extension and popup keyboard access

**Files:**
- Modify: `src/core/scripts/ScriptManifest.cpp`
- Modify: `src/app/Application.cpp`
- Modify: `CMakeLists.txt`
- Modify: `.gitignore`
- Create: `src/app/PopupMenuNavigation.h`
- Create: `tests/ScriptManifestTests.cpp`
- Create: `tests/PopupMenuNavigationTests.cpp`

**Interfaces:**
- Produces: `PopupMenuNavigation::Move(items, currentId, direction)` returning the next selectable item ID.

- [ ] **Step 1: Write failing tests**

```cpp
// A valid SAMPLE.TMSCRIPT is returned by LoadFromDirectory.
// Popup navigation skips separators, wraps, and returns the first selectable item.
```

- [ ] **Step 2: Add targets and verify failures**

Run the two new CTest targets; expect uppercase manifest loading and navigation compilation to fail.

- [ ] **Step 3: Implement minimal behavior**

Use `_wcsicmp(path.extension().c_str(), L".tmscript") == 0`. Handle `VK_UP`, `VK_DOWN`, `VK_RETURN`, `VK_RIGHT`, `VK_LEFT`, and `VK_ESCAPE` while the popup is open, invalidating only when selection changes.

- [ ] **Step 4: Run focused/full tests and commit**

```bash
git add .gitignore CMakeLists.txt src/core/scripts/ScriptManifest.cpp src/app/Application.cpp src/app/PopupMenuNavigation.h tests/ScriptManifestTests.cpp tests/PopupMenuNavigationTests.cpp
git commit -m "fix: make scripts and popup navigation consistent"
```

### Task 5: Dead helpers and bounded logs

**Files:**
- Modify: `src/app/AppUiHelpers.h`
- Modify: `src/app/AppUiHelpers.cpp`
- Modify: `src/core/common/LogFile.h`
- Modify: `src/core/common/LogFile.cpp`
- Test: `tests/LogFileTests.cpp`

**Interfaces:**
- Produces: `LogFile::Append(path, line, maxBytes = 1024 * 1024)`.

- [ ] **Step 1: Write a failing rotation test**

```cpp
Check(LogFile::Append(path, L"first long line", 16), "append rotates at limit");
Check(std::filesystem::exists(path + L".old"), "rotation keeps one backup");
Check(LogFile::Read(path) == L"first long line", "new log starts with current line");
```

- [ ] **Step 2: Verify failure**

Run: `cmake --build build --config Release --target TextMagicLogFileTests && ctest --test-dir build -C Release -R TextMagicLogFileTests --output-on-failure`.

- [ ] **Step 3: Implement one-backup rotation and delete dead helpers**

Rotate with `std::filesystem::remove` and `rename` before opening the append stream. Remove `FillListBoxWithText` and `GetSelectedListBoxText` declarations/definitions.

- [ ] **Step 4: Run full tests and commit**

```bash
git add src/app/AppUiHelpers.h src/app/AppUiHelpers.cpp src/core/common/LogFile.h src/core/common/LogFile.cpp tests/LogFileTests.cpp
git commit -m "chore: remove dead UI helpers and bound logs"
```

### Task 6: Final verification

**Files:**
- Review all files changed above.

- [ ] **Step 1: Clean build**

Run: `cmake --build build --config Release --clean-first`.

- [ ] **Step 2: Full test suite**

Run: `ctest --test-dir build -C Release --output-on-failure`.

- [ ] **Step 3: Diff and cleanliness checks**

Run: `git diff --check`, `git status --short`, and inspect `git diff HEAD~5..HEAD`.

- [ ] **Step 4: Refresh the portable codebase-memory artifact only if indexing succeeds**

Run the project indexer with persistence, stage only `.gitattributes`, `artifact.json`, and `graph.db.zst`, then commit the refreshed snapshot separately.
