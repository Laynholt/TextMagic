# Application Blacklist And Hotkey Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for every code task and superpowers:verification-before-completion before claiming completion.

**Goal:** Make repeated script hotkeys safe, make blacklisted/fullscreen applications receive hotkeys untouched, add persistent executable blacklist management, and eliminate log-window text overlap.

**Architecture:** Keep the existing Win32 application and low-level keyboard hook. Add one small persistent blacklist model and replace the cooldown-only helper with an execution gate. Keep platform enumeration, dialogs, and windows in `Application.cpp`; reuse `UiRenderer::DrawCustomCheckbox`, the existing info/message windows, and the session `LogFile` implementation.

**Tech Stack:** C++17, Win32 (`WH_KEYBOARD_LL`, Common Controls, `IFileOpenDialog`, `RICHEDIT50W`), CMake/CTest, existing GDI+/localization helpers.

## Global Constraints

- Do not add third-party dependencies or copy the `win32-custom-widgets` library.
- Preserve the existing modifier-double-tap rules, injected-key filtering, input modes, and session log truncation.
- A blocked foreground must bypass both script dispatch and keyboard input-buffer tracking, then return `CallNextHookEx`.
- A matched hotkey during busy/cooldown is consumed silently; it must not queue work, alter status, append a log, or show a dialog.
- Blacklist saves are transactional: mutate a copy, save it, then publish it to the live model.
- Run the smallest named test after each red/green step, then run the full Release suite before completion.

---

### Task 1: Add the persistent executable blacklist model

**Files:**

- Create: `src/core/common/ApplicationBlacklist.h`
- Create: `src/core/common/ApplicationBlacklist.cpp`
- Create: `tests/ApplicationBlacklistTests.cpp`
- Modify: `CMakeLists.txt:49-66` (`SOURCES`) and `CMakeLists.txt:133-170` (test targets)

**Step 1: Write the failing model tests**

Create one assert-style executable following `tests/LogFileTests.cpp`. Cover:

- a missing file loads successfully as an empty blacklist;
- UTF-8 with optional BOM and Cyrillic paths round-trips;
- blank, relative, non-`.exe`, and duplicate case variants are ignored;
- `Contains` uses case-insensitive ordinal comparison;
- `Add`/`Remove` change `Generation()` only when the set changes;
- a locked destination makes `Save` fail while the old file bytes remain unchanged.

Use a real absolute temp path and lock the saved target with `CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)` before the failure-preservation assertion.

Expected public API:

```cpp
class ApplicationBlacklist {
public:
    bool Load(const std::wstring& filePath, std::wstring* errorMessage);
    bool Save(const std::wstring& filePath, std::wstring* errorMessage) const;
    bool Add(const std::wstring& executablePath);
    bool Remove(const std::wstring& executablePath);
    bool Contains(const std::wstring& executablePath) const;
    const std::vector<std::wstring>& Paths() const noexcept;
    std::uint64_t Generation() const noexcept;

    static std::wstring NormalizeExecutablePath(const std::wstring& path);
    static bool PathsEqual(const std::wstring& left, const std::wstring& right) noexcept;

private:
    std::vector<std::wstring> m_paths;
    std::uint64_t m_generation = 1;
};
```

**Step 2: Register and run the failing test**

Add `TextMagicApplicationBlacklistTests` with `ApplicationBlacklist.cpp`, `EncodingUtils.cpp`, and the new test file.

Run:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target TextMagicApplicationBlacklistTests
ctest --test-dir build -C Release -R TextMagicApplicationBlacklistTests --output-on-failure
```

Expected: compilation fails because `ApplicationBlacklist` does not exist yet.

**Step 3: Implement the smallest model that passes**

Implementation rules:

- `NormalizeExecutablePath` trims surrounding whitespace, requires an absolute path with a case-insensitive `.exe` extension, then returns `std::filesystem::path(path).lexically_normal().wstring()`.
- `PathsEqual` calls `CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL`.
- `Load` treats `ERROR_FILE_NOT_FOUND` as success, decodes strict UTF-8 with `EncodingUtils::Utf8ToWide(bytes, true, false)`, splits `\r\n`/`\n`, and feeds each valid line through `Add`.
- Load into a temporary model and assign only after the complete read/decode succeeds.
- `Save` writes BOM plus one UTF-8 path per line to `filePath + L".tmp"`, flushes/closes, then calls:

```cpp
MoveFileExW(tempPath.c_str(), filePath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
```

- On any save failure, delete only the temporary sibling and leave the destination untouched.

Add `src/core/common/ApplicationBlacklist.cpp` to the main `SOURCES` list.

**Step 4: Run the model test**

Run the commands from Step 2 again.

Expected: `TextMagicApplicationBlacklistTests` passes.

**Step 5: Commit**

```powershell
git add CMakeLists.txt src/core/common/ApplicationBlacklist.h src/core/common/ApplicationBlacklist.cpp tests/ApplicationBlacklistTests.cpp
git commit -m "feat: add persistent application blacklist"
```

---

### Task 2: Replace the cooldown helper with an execution gate and dispatch policy

**Files:**

- Delete: `src/app/ScriptExecutionCooldown.h`
- Delete: `tests/ScriptExecutionCooldownTests.cpp`
- Create: `src/app/ScriptExecutionGate.h`
- Create: `tests/ScriptExecutionGateTests.cpp`
- Modify: `CMakeLists.txt:160-170`

**Step 1: Write the failing gate test**

Test these state transitions with explicit millisecond values:

```cpp
ScriptExecutionGate gate;
Expect(gate.TryReserve(100), "first dispatch must reserve");
Expect(!gate.TryReserve(101), "busy dispatch must be suppressed");
gate.Release(200);
Expect(!gate.TryReserve(449), "cooldown dispatch must be suppressed");
Expect(gate.TryReserve(450), "dispatch after cooldown must reserve");
```

Also test the pure policy used by the hook:

```cpp
Expect(!HotkeyDispatch::ShouldTrackInput(true), "blocked input must bypass tracking");
Expect(HotkeyDispatch::Decide(true, true, false) == HotkeyDispatch::Action::PassThrough,
       "blocked hotkey must pass through");
Expect(HotkeyDispatch::Decide(false, true, false) == HotkeyDispatch::Action::Consume,
       "busy matched hotkey must be consumed");
Expect(HotkeyDispatch::Decide(false, true, true) == HotkeyDispatch::Action::Dispatch,
       "reserved matched hotkey must dispatch");
```

**Step 2: Rename the CMake test target and prove red**

Rename the target/test to `TextMagicScriptExecutionGateTests`, point it at the new file, then run:

```powershell
cmake --build build --config Release --target TextMagicScriptExecutionGateTests
ctest --test-dir build -C Release -R TextMagicScriptExecutionGateTests --output-on-failure
```

Expected: compilation fails because `ScriptExecutionGate.h` is missing.

**Step 3: Implement the header-only gate**

Keep the replacement in one header:

```cpp
class ScriptExecutionGate {
public:
    static constexpr std::uint64_t CooldownMs = 250;

    bool TryReserve(std::uint64_t now) noexcept {
        if (m_reserved || (m_lastRelease != 0 && now - m_lastRelease < CooldownMs)) {
            return false;
        }
        m_reserved = true;
        return true;
    }

    void Release(std::uint64_t now) noexcept {
        m_reserved = false;
        m_lastRelease = now;
    }

    bool IsReserved() const noexcept { return m_reserved; }

private:
    bool m_reserved = false;
    std::uint64_t m_lastRelease = 0;
};
```

In the same header, add only the three-value `HotkeyDispatch::Action`, `ShouldTrackInput(bool blocked)`, and `Decide(bool blocked, bool matched, bool reservationSucceeded)` constexpr helpers required by the test and hook.

**Step 4: Run the gate test**

Run Step 2 again. Expected: pass.

**Step 5: Commit**

```powershell
git add -A CMakeLists.txt src/app/ScriptExecutionCooldown.h src/app/ScriptExecutionGate.h tests/ScriptExecutionCooldownTests.cpp tests/ScriptExecutionGateTests.cpp
git commit -m "refactor: gate repeated script hotkeys"
```

---

### Task 3: Route every script hotkey through the hook and contain worker failures

**Files:**

- Modify: `src/app/Application.h:15-21, 35-46, 94-113, 174-203`
- Modify: `src/app/Application.cpp:1-28, 226-520, 651-665, 1330-1480, 1602-1694, 3031-3105, 3535-3670`
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`

**Step 1: Add a focused regression assertion before integration**

Extend `tests/ScriptExecutionGateTests.cpp` with a sequence that reserves once, rejects 100 spam attempts at the same tick, releases, rejects 100 cooldown attempts, and accepts exactly once at `release + CooldownMs`.

Run:

```powershell
cmake --build build --config Release --target TextMagicScriptExecutionGateTests
ctest --test-dir build -C Release -R TextMagicScriptExecutionGateTests --output-on-failure
```

Expected: pass against the gate; this pins the contract before Win32 wiring.

**Step 2: Add live blacklist and gate ownership**

In `Application.h`:

- include `ApplicationBlacklist.h` and `ScriptExecutionGate.h`;
- replace `m_scriptExecutionInProgress` and `m_lastScriptCompletionTick` with `ApplicationBlacklist m_applicationBlacklist`, `ScriptExecutionGate m_scriptExecutionGate`, and `std::wstring m_blacklistPath`;
- change `ExecuteScript` to `void ExecuteScript(const RegisteredScript&, bool clipboardOnly, bool reservationHeld = false);`.

In the namespace state in `Application.cpp`, retain the existing hook vectors/mutexes and add only pointers to the two `Application`-owned objects plus the fullscreen flag:

```cpp
ApplicationBlacklist* g_applicationBlacklist = nullptr;
ScriptExecutionGate* g_scriptExecutionGate = nullptr;
bool g_disableHotkeysInFullscreen = false;
```

Set them after successful blacklist load and clear them before hook uninstall in `Shutdown`.

Load `TextMagic.blacklist` beside the executable after the single-instance check and `LogFile::Clear`. Save any load error text and append one localized warning after normal startup logging begins.

**Step 3: Add cached foreground blocking**

Add namespace helpers beside the hook code:

```cpp
bool TryGetWindowExecutablePath(HWND window, std::wstring* path);
bool IsForegroundHandlingBlocked();
void InvalidateForegroundBlockCache();
```

`TryGetWindowExecutablePath` must use `GetWindowThreadProcessId`, `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`, a 32768-character buffer, and `QueryFullProcessImageNameW`.

`IsForegroundHandlingBlocked` must:

1. return true immediately when the existing fullscreen option is enabled and `FullscreenUtils::IsForegroundWindowFullscreen()` is true;
2. cache blacklist lookup by foreground `HWND` and `ApplicationBlacklist::Generation()`;
3. fail open when process/path lookup fails.

Do not perform file I/O from this path.

**Step 4: Make hook dispatch reserve before posting**

At the start of `InputKeyboardHookProc`, before matching or `HandleInputBufferKeyDown`, check the blocked state. When blocked, return `CallNextHookEx` for key-down and key-up without input tracking.

Change `DispatchHookHotkeysOnKeyDown` from `bool` to `HotkeyDispatch::Action`:

- no match returns `PassThrough`;
- a match calls `g_scriptExecutionGate->TryReserve(GetTickCount64())`;
- rejection returns `Consume` without posting/logging/status;
- successful `PostMessageW(g_hotkeyDispatchWindow, WM_HOTKEY, hotkey.hotkeyId, 0)` disarms the hotkey and returns `Dispatch`;
- post failure releases the reservation and returns `Consume`.

In the hook proc, both `Consume` and `Dispatch` return `1`; only `PassThrough` reaches input tracking and `CallNextHookEx`.

**Step 5: Remove `RegisterHotKey` entirely**

In `RegisterHotkeys`, preserve disabled/invalid/duplicate checks, but add every enabled unique shortcut with `AddHookHotkey`. Mark it registered and keep `AddTrackedHotkey` for input-buffer exclusion.

In `UnregisterHotkeys`, remove the `UnregisterHotKey` loop and only clear maps/vectors. Remove the now-unused `IsModifierVirtualKey` and `app.error.register_hotkey_prefix` path.

**Step 6: Hold the reservation through complete result handling**

`ExecuteScriptByHotkeyId` now assumes the hook already reserved. Invalid, stale, or disabled IDs must call `m_scriptExecutionGate.Release(GetTickCount64())` before returning.

Manual execution calls `ExecuteScript(selectedScript, true, false)`; `ExecuteScript` reserves when `reservationHeld == false` and shows the existing “already running” status only for this manual path.

Wrap the body of `WM_SCRIPT_EXECUTION_COMPLETE` in an immediately invoked lambda so its existing early returns leave only the lambda. Release after the lambda returns:

```cpp
case WM_SCRIPT_EXECUTION_COMPLETE:
    [&]() {
        std::unique_ptr<ScriptExecutionTaskResult> result(
            reinterpret_cast<ScriptExecutionTaskResult*>(wParam));
        if (!result) return;
        // existing result/status/replacement/dialog handling
    }();
    m_scriptExecutionGate.Release(GetTickCount64());
    return 0;
```

This deliberately keeps the gate held until any modal error dialog closes.

**Step 7: Contain both worker-body and thread-creation exceptions**

Construct the worker as a named `std::thread` inside a `try` block, then detach it. Inside the lambda, use an outermost `try/catch` so no exception crosses the thread entry point. In the normal outer block, allocate the result with `std::unique_ptr`, wrap source-selection and script execution in inner `catch (const std::exception&)` / `catch (...)`, translate `what()` with `EncodingUtils::Utf8ToWide`, set `executeOk = false`, and post the owned result exactly once. If even result construction/error formatting fails, the outer catch posts `WM_SCRIPT_EXECUTION_COMPLETE` with a null result so the UI thread still releases the gate.

Catch `std::system_error` around thread construction on the UI thread. On failure, release the gate, set one localized error status, and append one error log; do not open a modal dialog from this synchronous failure path.

Add English/Russian keys for blacklist load warning, standard worker exception, unknown worker exception, and thread-start failure. Keep the existing no-text path non-modal.

**Step 8: Build and run automated tests**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: build succeeds and all tests pass.

**Step 9: Commit**

```powershell
git add src/app/Application.h src/app/Application.cpp lang/en.ini lang/ru.ini tests/ScriptExecutionGateTests.cpp
git commit -m "fix: suppress repeated hotkey execution safely"
```

---

### Task 4: Build the blacklist management UI and application selectors

**Files:**

- Create: `src/app/RunningApplication.h`
- Create: `tests/RunningApplicationTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/app/Application.h:35-46, 113-128, 145-203`
- Modify: `src/app/Application.cpp:30-140, 1070-1120, 1325-1480, 2780-2820, 3760-4400, 4402-4636`
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`

**Step 1: Write the failing running-application model test**

Create a header-only `RunningApplication` model contract used by both test and UI:

```cpp
struct RunningApplication {
    std::wstring executableName;
    std::wstring windowTitle;
    std::wstring path;
};

std::vector<RunningApplication> DeduplicateRunningApplications(
    const std::vector<RunningApplication>& applications);
```

The test supplies empty paths and same-path case variants, then asserts that the helper skips empties, preserves first-seen order/title, and compares paths through `ApplicationBlacklist::PathsEqual`.

Register `TextMagicRunningApplicationTests` and run:

```powershell
cmake --build build --config Release --target TextMagicRunningApplicationTests
ctest --test-dir build -C Release -R TextMagicRunningApplicationTests --output-on-failure
```

Expected: compilation fails until the header/helper exists.

**Step 2: Implement the minimal header helper**

Implement the single linear deduplication function inline with `std::find_if`; do not add a service/interface. Run Step 1 again and expect pass.

**Step 3: Rename the exclusions feature in code and localization**

Rename:

- `InfoWindowKind::HotkeyExclusions` to `ApplicationBlacklist`;
- `ID_MENU_HOTKEY_EXCLUSIONS` to `ID_MENU_APPLICATION_BLACKLIST`;
- `m_hHotkeyExclusionsWindow` and `ShowHotkeyExclusionsWindow` to blacklist equivalents;
- menu/title/checkbox localization keys from `hotkey_exclusions` to `application_blacklist`.

Use these exact user-visible strings:

```ini
menu.application_blacklist=Application blacklist...
application_blacklist.title=Application blacklist
application_blacklist.disable_fullscreen=Disable hotkeys in fullscreen applications
application_blacklist.column.application=Application
application_blacklist.column.path=Path
application_blacklist.running=Running applications...
application_blacklist.add_exe=Add .exe...
application_blacklist.remove=Remove
application_blacklist.add_selected=Add selected
application_blacklist.save_error=Failed to save the application blacklist.
```

Add the matching Russian strings from the approved design (`Чёрный список приложений...`, `Приложение`, `Путь`, `Из запущенных...`, `Добавить .exe...`, `Удалить`, `Добавить выбранные`).

**Step 4: Turn the info window into the resizable blacklist manager**

Extend `InfoWindowState` with a report `LISTVIEW` and three action buttons. Add control IDs for the list, running picker, `.exe` picker, and remove action.

For blacklist kind in `WM_CREATE`:

- keep a native `BUTTON` with `BS_AUTOCHECKBOX`;
- create `WC_LISTVIEWW` with `LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS`;
- insert Application and Path columns;
- create owner-drawn action buttons using the existing button renderer;
- populate rows from `m_applicationBlacklist.Paths()` using `std::filesystem::path(path).filename()`.

Start near 760x520, use the existing `LOGS_MIN_WIDTH/HEIGHT` minimums, and resize the list/buttons in `WM_SIZE`. Disable Remove when no item is selected; handle `LVN_ITEMCHANGED` and Delete key (`LVN_KEYDOWN`).

Apply the existing dark brushes plus list-view colors/custom draw; do not add another rendering class.

**Step 5: Wire the existing custom checkbox renderer**

Keep native auto-check behavior and install one `SetWindowSubclass` paint shim. Its state only tracks hover via `TrackMouseEvent`. On `WM_PAINT`, read checked/pressed/enabled/focused from `BM_GETCHECK`, `BM_GETSTATE`, `IsWindowEnabled`, and `GetFocus`, then call the already existing:

```cpp
UiRenderer::DrawCustomCheckbox(
    hdc, hWnd, label, checked, hot, pressed, enabled, focused);
```

Forward mouse/keyboard/accessibility behavior to `DefSubclassProc`, invalidate after state changes, and delete the tiny visual state on `WM_NCDESTROY`.

When the checkbox changes, save the existing fullscreen setting and update `g_disableHotkeysInFullscreen` immediately.

**Step 6: Persist add/remove transactionally**

For every add/remove operation:

1. copy `m_applicationBlacklist`;
2. apply all requested mutations to the copy;
3. if nothing changed, return;
4. save the copy to `m_blacklistPath`;
5. on success move-assign it, invalidate the foreground cache, and refresh the list;
6. on failure keep the live model/list unchanged and show the localized save error plus technical detail.

This avoids rollback code and guarantees memory matches the last persisted file.

**Step 7: Enumerate visible running applications**

Add a namespace `EnumWindows` callback in `Application.cpp` that accepts only visible top-level windows with non-empty titles, resolves each path with the Step 3 helper, creates `RunningApplication` rows, then calls `DeduplicateRunningApplications`.

Reuse `MessageWindowProc` rather than register another window class:

- add a `runningApplicationSelection` mode and output path pointer to `MessageWindowState`;
- in that mode create an owner-drawn `LBS_EXTENDEDSEL` list and fill each row as `name — title — path`;
- primary button copies all selected model paths and closes;
- secondary button cancels without output.

Guard the existing wrapped-message `FillListBoxWithWrappedText` calls so create/resize never overwrite application rows in selection mode.

Keep its existing dark card, button painting, modal message loop, and resizing.

**Step 8: Add the modern multi-select `.exe` picker**

In `Application.cpp`, create `IFileOpenDialog`, set:

```cpp
FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT
```

and `COMDLG_FILTERSPEC{L"Applications (*.exe)", L"*.exe"}`. Read `IShellItemArray` results with `SIGDN_FILESYSPATH`, release every COM object/string, and pass the collected paths through the same transactional add helper. Treat user cancellation as a no-op; report other HRESULT failures through the styled localized error dialog.

**Step 9: Run tests and build**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: all tests pass and `build/bin/Release/TextMagic.exe` is produced.

**Step 10: Commit**

```powershell
git add CMakeLists.txt src/app/RunningApplication.h tests/RunningApplicationTests.cpp src/app/Application.h src/app/Application.cpp lang/en.ini lang/ru.ini
git commit -m "feat: add application blacklist manager"
```

---

### Task 5: Replace the lagging log `EDIT` with RichEdit

**Files:**

- Modify: `src/app/Application.h:145-203`
- Modify: `src/app/Application.cpp:1-28, 88-101, 1330-1480, 4012-4048, 4074-4375`
- Modify: `CMakeLists.txt:88-101` only if the SDK requires an explicit RichEdit library (normally it does not)

**Step 1: Preserve the file-source contract with the existing test**

Run before editing:

```powershell
cmake --build build --config Release --target TextMagicLogFileTests
ctest --test-dir build -C Release -R TextMagicLogFileTests --output-on-failure
```

Expected: pass, proving session truncate/append/read behavior is already correct.

**Step 2: Load and own the RichEdit module**

Include `<richedit.h>`, add `HMODULE m_msfteditModule = nullptr`, call `LoadLibraryW(L"Msftedit.dll")` during initialization, and free it during shutdown only after the info windows are destroyed.

Add `bool richEdit = false` and `bool logPlaceholderVisible = false` to `InfoWindowState`.

**Step 3: Create and style the log control**

For logs, call `CreateWindowExW(0, MSFTEDIT_CLASS, state->text.c_str(), logStyles, 0, 0, 100, 100, hWnd, reinterpret_cast<HMENU>(ID_INFO_TEXT), GetModuleHandleW(nullptr), nullptr)` first with the existing read-only multiline/scroll styles. On success:

- set `richEdit = true`;
- set `EM_SETBKGNDCOLOR` to `RGB(45,45,45)`;
- send `EM_SETCHARFORMAT`/`SCF_ALL` with `CFM_COLOR` and `RGB(245,245,245)`;
- keep the existing monospaced font, high text limit, and left/right margins;
- do not call `ApplyDarkScrollBar` or apply the Explorer theme.

If RichEdit creation fails, create the current plain `EDIT`, call `SetWindowTheme(control, L"", L"")`, and keep existing color-brush handling.

**Step 4: Append only the persisted line**

Keep `LogFile::Append` first. For an open log control, append `\r\n + persistedLine` with `EM_SETSEL`/`EM_REPLACESEL`; do not grow `InfoWindowState::text` for logs. Initialize `logPlaceholderVisible` when the window opens, set it again on Clear, and use it to replace the localized empty placeholder exactly once with the first persisted line.

For the fallback plain `EDIT`, call:

```cpp
RedrawWindow(control, nullptr, nullptr,
             RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
```

after append. Scroll the caret as today.

In Save As handling, pass `LogFile::Read(state->owner->m_logPath)` instead of `state->text`, so disk remains authoritative and an empty log saves as an empty file rather than the localized UI placeholder.

**Step 5: Run the log test and full build**

```powershell
cmake --build build --config Release --target TextMagicLogFileTests
ctest --test-dir build -C Release -R TextMagicLogFileTests --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: all pass.

**Step 6: Commit**

```powershell
git add src/app/Application.h src/app/Application.cpp CMakeLists.txt
git commit -m "fix: render execution logs with RichEdit"
```

---

### Task 6: Release verification and local handoff

**Files:**

- Verify: `build/bin/Release/TextMagic.exe`
- Verify: `build/bin/Release/TextMagic.blacklist` (created only after first saved entry)
- Verify: `build/bin/Release/TextMagic.log`
- Verify: working tree and branch history

**Step 1: Run the complete clean-enough Release verification**

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --clean-first
ctest --test-dir build -C Release --output-on-failure
```

Expected: Release executable builds and every CTest target passes.

**Step 2: Perform manual hotkey smoke tests**

In both Sublime Text and Notepad:

- spam a script hotkey with source text and confirm only complete replacements occur;
- spam it with no source text and confirm there are no control characters/newlines, nested dialogs, hangs, or termination;
- confirm the first accepted invocation runs, busy/cooldown repeats are silent, and the hotkey works again after cooldown;
- verify both previous-word and all-text input modes.

**Step 3: Perform blacklist smoke tests**

- add multiple visible applications through Running applications;
- add one or more executables with the modern picker;
- select/delete rows and verify immediate persistence;
- restart and verify `TextMagic.blacklist` reloads;
- while a blacklisted or fullscreen application is foreground, verify the original hotkey reaches it and TextMagic neither runs nor tracks its typed text;
- remove the entry and verify TextMagic handles the hotkey again.

**Step 4: Perform log-window smoke tests**

- generate many log lines, resize, scroll, clear, close, and reopen the logs window;
- verify no glyph overlap or sluggish full-text rebuild on each append;
- Save As and compare its contents with the current session `TextMagic.log`;
- restart and verify the old session was truncated.

**Step 5: Inspect the final diff and status**

```powershell
git diff --check
git status --short --branch
git log --oneline --decorate -6
```

Expected: no whitespace errors; only the known untracked `.codebase-memory/` remains outside the implementation commits.

**Step 6: Commit any verification-only corrections, then report**

If smoke testing required a correction, repeat the relevant smallest test plus the full Release suite, then commit only that correction. Report the exact executable path, test count/result, branch, and commits. Do not merge to `master` until explicitly requested.
