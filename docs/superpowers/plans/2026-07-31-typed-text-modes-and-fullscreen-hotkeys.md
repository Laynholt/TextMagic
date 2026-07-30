# Typed Text Modes and Fullscreen Hotkeys Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement true last-word/all-typed-text modes, efficient deletion above 500 characters, list padding, and optional fullscreen hotkey suppression.

**Architecture:** Extend the existing `InputBuffer::PreviousWordCapture` path so both input modes use the same context-checked replacement flow. Keep short deletion unchanged and switch long deletion inside `TextBridge::DeleteCharacters`, the shared destructive boundary. Add one small Win32 fullscreen helper so rectangle classification is unit-testable, then gate both hotkey mechanisms at `ExecuteScriptByHotkeyId`.

**Tech Stack:** C++17, Win32 API, CMake/CTest, existing INI localization and settings.

## Global Constraints

- Work directly on `master`; do not create another worktree.
- Keep persisted input-mode values `previous_word` and `all_text`.
- Track at most 20,000 UTF-16 code units; clear the whole session on overflow.
- Use repeated Backspace through 500 characters and exact `Shift+Left` selection above 500.
- Do not use clipboard paste for input-buffer replacement.
- Fullscreen suppression is disabled by default and does not include ordinary maximized windows.
- Do not implement the future executable blacklist.
- Add no dependencies and do not touch the untracked `.codebase-memory/` directory.

---

### Task 1: Make both input modes use the tracked session

**Files:**
- Modify: `src/core/text/InputBuffer.h`
- Modify: `src/core/text/InputBuffer.cpp`
- Modify: `tests/InputBufferTests.cpp`
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`

**Interfaces:**
- Consumes: existing `InputBuffer::PreviousWordCapture`, `IsCaptureCurrent`, and `CommitReplacement`.
- Produces: `bool InputBuffer::TryPeekAllText(ContextId, PreviousWordCapture*) const`.
- Produces: one input-buffer replacement path in `Application` for both `previous_word` and `all_text`.

- [ ] **Step 1: Add failing full-session and overflow tests**

Append these checks to `tests/InputBufferTests.cpp` before `return 0`:

```cpp
    buffer.Clear();
    buffer.AppendText(editorContext, L"one two three");
    Expect(buffer.TryPeekAllText(editorContext, &capture), "expected all-text capture");
    Expect(capture.word == L"one two three", "all-text capture must contain the complete session");
    Expect(capture.trailing.empty(), "all-text capture must not add trailing text");
    Expect(capture.deleteChars == 13 && capture.replaceOffset == 0,
           "all-text capture must replace the complete session");
    Expect(buffer.CommitReplacement(editorContext, capture, L"ONE TWO THREE"),
           "expected all-text replacement commit");
    Expect(buffer.TextForTest() == L"ONE TWO THREE", "expected committed all-text replacement");

    buffer.PopCharacter(editorContext);
    Expect(buffer.TextForTest() == L"ONE TWO THRE", "Backspace must remove one tracked character");

    buffer.Clear();
    buffer.AppendText(editorContext, std::wstring(20000, L'x'));
    Expect(buffer.TextForTest().size() == 20000, "20,000 characters must remain tracked");
    buffer.AppendText(editorContext, L"y");
    Expect(buffer.TextForTest().empty(), "overflow must clear the whole session");
    buffer.AppendText(editorContext, L"z");
    Expect(buffer.TextForTest() == L"z", "typing after overflow must start a new session");
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicInputBufferTests
```

Expected: compilation fails because `TryPeekAllText` does not exist.

- [ ] **Step 3: Implement complete-session capture and reset-on-overflow**

Add to `InputBuffer.h`:

```cpp
    bool TryPeekAllText(ContextId contextId, PreviousWordCapture* capture) const;
```

Implement in `InputBuffer.cpp`:

```cpp
namespace {
constexpr size_t MAX_INPUT_BUFFER_CHARS = 20000;
}

bool InputBuffer::TryPeekAllText(ContextId contextId, PreviousWordCapture* capture) const {
    if (capture) {
        *capture = PreviousWordCapture();
    }
    if (contextId == 0 || contextId != m_contextId || m_text.empty()) {
        return false;
    }
    if (capture) {
        capture->word = m_text;
        capture->deleteChars = m_text.size();
        capture->replaceOffset = 0;
        capture->expectedSize = m_text.size();
        capture->contextId = contextId;
        capture->generation = m_generation;
    }
    return true;
}
```

Replace tail trimming with full reset:

```cpp
void InputBuffer::ClearIfOverLimit() {
    if (m_text.size() > MAX_INPUT_BUFFER_CHARS) {
        Clear();
    }
}
```

Rename `TrimToLimit` to `ClearIfOverLimit` in the header and both callers.

- [ ] **Step 4: Run the focused test and verify it passes**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicInputBufferTests
rtk ctest --test-dir build -C Debug -R TextMagicInputBufferTests --output-on-failure
```

Expected: `TextMagicInputBufferTests` passes.

- [ ] **Step 5: Route both modes through one capture replacement path**

In `Application.cpp`, add a mutex-protected wrapper:

```cpp
bool PeekAllTextFromInputBuffer(
    InputBuffer::ContextId contextId,
    InputBuffer::PreviousWordCapture* capture
) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    return g_inputBuffer.TryPeekAllText(contextId, capture);
}
```

Rename result fields from `previousWordCapture`/`previousWordMode` to
`inputCapture`/`inputBufferMode`, and add `bool allTextInputMode = false`.
At hotkey execution, choose the capture from the saved mode:

```cpp
    const bool allTextInputMode = m_scriptInputAllText;
    InputBuffer::PreviousWordCapture inputCapture;
    const bool hasInputCapture = !useClipboardOnly
        && (allTextInputMode
            ? PeekAllTextFromInputBuffer(CurrentInputContext(), &inputCapture)
            : PeekPreviousWordFromInputBuffer(CurrentInputContext(), &inputCapture))
        && !inputCapture.word.empty();
```

When `hasInputCapture` is true, run the script with `inputCapture.word`. On
completion, use the existing modifier/context checks, delete
`inputCapture.deleteChars`, type `outputText + inputCapture.trailing`, and commit
through `CommitPreviousWordReplacement`. Remove the no-selection
`GetAllText`/`SetAllText` fallback; explicit selection remains on the existing
clipboard path.

Rename `m_scriptInputFallbackToAllText` to `m_scriptInputAllText` and update local
parameter names without changing the INI values.

- [ ] **Step 6: Update visible mode names**

Change these localization values:

```ini
# lang/en.ini
menu.input_mode.previous_word=Last typed word
menu.input_mode.all_text=All typed text
app.status.input_mode_previous_word=Input mode: last typed word.
app.status.input_mode_all_text=Input mode: all typed text.

# lang/ru.ini
menu.input_mode.previous_word=Последнее введённое слово
menu.input_mode.all_text=Весь введённый текст
app.status.input_mode_previous_word=Режим ввода: последнее введённое слово.
app.status.input_mode_all_text=Режим ввода: весь введённый текст.
```

- [ ] **Step 7: Build and run the focused test**

Run:

```powershell
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug -R TextMagicInputBufferTests --output-on-failure
```

Expected: build succeeds and the focused test passes.

- [ ] **Step 8: Commit**

```powershell
rtk git add -- src/core/text/InputBuffer.h src/core/text/InputBuffer.cpp tests/InputBufferTests.cpp src/app/Application.h src/app/Application.cpp lang/en.ini lang/ru.ini
rtk git commit -m "feat: track complete typed text sessions"
```

---

### Task 2: Select long input ranges before deletion

**Files:**
- Modify: `src/core/text/TextBridge.h`
- Modify: `src/core/text/TextBridge.cpp`

**Interfaces:**
- Consumes: `TextBridge::DeleteCharacters(size_t)`.
- Produces: private `bool SelectPreviousCharacters(size_t) const`.
- Preserves: callers continue using `DeleteCharacters` without knowing the strategy.

- [ ] **Step 1: Add compile-time boundary checks**

Add to the anonymous namespace in `TextBridge.cpp`:

```cpp
constexpr size_t DIRECT_BACKSPACE_LIMIT = 500;

constexpr bool ShouldSelectBeforeDelete(size_t count) {
    return count > DIRECT_BACKSPACE_LIMIT;
}

static_assert(!ShouldSelectBeforeDelete(500));
static_assert(ShouldSelectBeforeDelete(501));
```

- [ ] **Step 2: Implement batched exact selection**

Add the private declaration:

```cpp
    bool SelectPreviousCharacters(size_t count) const;
```

Include `<limits>` and `<vector>`, then construct one `INPUT` vector containing
Shift down, `count` Left down/up pairs, and Shift up. Require
`count <= (std::numeric_limits<UINT>::max() - 2) / 2`.

On a partial `SendInput`, explicitly send Shift-up and one unmodified Right key to
collapse any partial selection at its original right edge, then return `false`.

- [ ] **Step 3: Switch the shared deletion boundary**

Replace `DeleteCharacters` with:

```cpp
bool TextBridge::DeleteCharacters(size_t count) const {
    if (count == 0) {
        return true;
    }
    if (!ShouldSelectBeforeDelete(count)) {
        return SendRepeatedKey(VK_BACK, count);
    }
    if (!SelectPreviousCharacters(count)) {
        return false;
    }
    if (SendKey(VK_BACK)) {
        return true;
    }
    SendKey(VK_RIGHT);
    return false;
}
```

- [ ] **Step 4: Build and run input-buffer tests**

Run:

```powershell
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug -R TextMagicInputBufferTests --output-on-failure
```

Expected: compilation succeeds, including both threshold `static_assert`s, and the
test passes.

- [ ] **Step 5: Commit**

```powershell
rtk git add -- src/core/text/TextBridge.h src/core/text/TextBridge.cpp
rtk git commit -m "perf: select long text before replacement"
```

---

### Task 3: Add visual padding to scripts and logs

**Files:**
- Modify: `src/ui/UiRenderer.h`
- Modify: `src/ui/UiRenderer.cpp`
- Modify: `src/app/Application.cpp`

**Interfaces:**
- Consumes: native list boxes and `UiRenderer::DrawEditBorder`.
- Produces: `DrawEditBorder(HWND, HWND, int padding = 0)`.

- [ ] **Step 1: Allow the existing border helper to surround an inset control**

Change the declaration:

```cpp
static void DrawEditBorder(HWND parentWindow, HWND editControl, int padding = 0);
```

After converting the child rectangle to parent coordinates in
`UiRenderer.cpp`, expand it:

```cpp
InflateRect(&rect, std::max(0, padding), std::max(0, padding));
```

Include `<algorithm>` if it is not already present.

- [ ] **Step 2: Inset only the requested list boxes**

Add:

```cpp
constexpr int LIST_CONTENT_PADDING = 6;
```

Move the main script list to:

```cpp
MoveWindow(
    m_hScriptList,
    innerX + LIST_CONTENT_PADDING,
    listTop + LIST_CONTENT_PADDING,
    std::max(1, innerWidth - 2 * LIST_CONTENT_PADDING),
    std::max(1, listHeight - 2 * LIST_CONTENT_PADDING),
    TRUE
);
```

Draw its border with `LIST_CONTENT_PADDING`. Apply the same inset and expanded
border only to the logs list in `InfoWindowProc`; leave About and message-window
controls unchanged.

- [ ] **Step 3: Build and visually inspect**

Run:

```powershell
rtk cmake --build build --config Debug
```

Open the Debug application and verify a visible six-pixel inset on all four sides
of the script and log lists, with native selection and scrolling unchanged.

- [ ] **Step 4: Commit**

```powershell
rtk git add -- src/ui/UiRenderer.h src/ui/UiRenderer.cpp src/app/Application.cpp
rtk git commit -m "style: pad script and log lists"
```

---

### Task 4: Suppress script hotkeys in true fullscreen windows

**Files:**
- Create: `src/app/FullscreenUtils.h`
- Create: `src/app/FullscreenUtils.cpp`
- Create: `tests/FullscreenUtilsTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`

**Interfaces:**
- Produces: `FullscreenUtils::IsFullscreenBounds(const RECT&, const RECT&, bool)`.
- Produces: `FullscreenUtils::IsForegroundWindowFullscreen(HWND ignoredWindow)`.
- Consumes: the common `Application::ExecuteScriptByHotkeyId` path used by native and hook hotkeys.

- [ ] **Step 1: Add a failing classifier test and CTest target**

Create `tests/FullscreenUtilsTests.cpp`:

```cpp
#include "FullscreenUtils.h"

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}
}

int main() {
    const RECT monitor = { 0, 0, 1920, 1080 };
    const RECT fullscreen = { 0, 0, 1920, 1080 };
    const RECT maximizedWorkArea = { 0, 0, 1920, 1040 };
    const RECT covering = { -1, -1, 1921, 1081 };

    Expect(FullscreenUtils::IsFullscreenBounds(fullscreen, monitor, false),
           "exact monitor coverage must be fullscreen");
    Expect(FullscreenUtils::IsFullscreenBounds(covering, monitor, false),
           "covering monitor bounds must be fullscreen");
    Expect(!FullscreenUtils::IsFullscreenBounds(maximizedWorkArea, monitor, false),
           "work-area maximization must not be fullscreen");
    Expect(!FullscreenUtils::IsFullscreenBounds(fullscreen, monitor, true),
           "ordinary maximized windows must not be fullscreen");
    return 0;
}
```

Add a `TextMagicFullscreenUtilsTests` executable using
`src/app/FullscreenUtils.cpp`, include `src/app`, link `user32`, and register it
with `add_test`.

- [ ] **Step 2: Configure/build and verify the test fails**

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Debug --target TextMagicFullscreenUtilsTests
```

Expected: build fails because `FullscreenUtils.h/.cpp` do not exist.

- [ ] **Step 3: Implement the minimal Win32 classifier**

Create `src/app/FullscreenUtils.h`:

```cpp
#pragma once

#include <windows.h>

namespace FullscreenUtils {
bool IsFullscreenBounds(const RECT& windowRect,
                        const RECT& monitorRect,
                        bool ordinaryMaximized);
bool IsForegroundWindowFullscreen(HWND ignoredWindow);
}
```

`IsFullscreenBounds` returns false for `ordinaryMaximized`; otherwise it requires
the window rectangle to cover all four monitor bounds. The foreground helper
rejects null, ignored, desktop, shell, and minimized windows; obtains
`GetWindowRect`, `MonitorFromWindow(..., MONITOR_DEFAULTTONEAREST)`, and
`GetMonitorInfoW`; and marks a window as ordinarily maximized when `IsZoomed` is
true and its style contains `WS_OVERLAPPEDWINDOW`.

- [ ] **Step 4: Run the classifier test**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicFullscreenUtilsTests
rtk ctest --test-dir build -C Debug -R TextMagicFullscreenUtilsTests --output-on-failure
```

Expected: `TextMagicFullscreenUtilsTests` passes.

- [ ] **Step 5: Add the persisted checked menu item**

In `Application.cpp`:

```cpp
constexpr const wchar_t* DISABLE_FULLSCREEN_HOTKEYS_SETTINGS_KEY =
    L"disable_hotkeys_in_fullscreen";
```

Load it with `GetPrivateProfileIntW(..., 0, ...) != 0`, save `L"1"` or `L"0"` with
`WritePrivateProfileStringW`, and store it in:

```cpp
bool m_disableHotkeysInFullscreen = false;
```

Add menu ID `ID_MENU_DISABLE_HOTKEYS_FULLSCREEN = 2020`, map it to localization
key `menu.disable_hotkeys_fullscreen`, include it in styled menu items and
`BuildMainMorePopupItems`, and toggle/save it from `OnMenuCommand`.

At the start of `ExecuteScriptByHotkeyId`, before any script work:

```cpp
    if (m_disableHotkeysInFullscreen
        && FullscreenUtils::IsForegroundWindowFullscreen(m_hWnd)) {
        return;
    }
```

Do not add status or log output for suppression.

- [ ] **Step 6: Add localization**

```ini
# lang/en.ini
menu.disable_hotkeys_fullscreen=Disable hotkeys in fullscreen applications

# lang/ru.ini
menu.disable_hotkeys_fullscreen=Отключать горячие клавиши в полноэкранных приложениях
```

- [ ] **Step 7: Build and run both focused tests**

Run:

```powershell
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug -R "TextMagic(InputBuffer|FullscreenUtils)Tests" --output-on-failure
```

Expected: build succeeds and both tests pass.

- [ ] **Step 8: Commit**

```powershell
rtk git add -- CMakeLists.txt src/app/FullscreenUtils.h src/app/FullscreenUtils.cpp tests/FullscreenUtilsTests.cpp src/app/Application.h src/app/Application.cpp lang/en.ini lang/ru.ini
rtk git commit -m "feat: suppress hotkeys in fullscreen apps"
```

---

### Task 5: Final verification

**Files:**
- Verify only; modify a task-owned file only if a check exposes a defect.

**Interfaces:**
- Consumes: all completed tasks.
- Produces: a tested Debug build on `master`.

- [ ] **Step 1: Run formatting and repository checks**

```powershell
rtk git diff --check HEAD~4
rtk git status --short
```

Expected: no whitespace errors; only the pre-existing untracked
`.codebase-memory/` remains.

- [ ] **Step 2: Build and run the full suite**

```powershell
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug --output-on-failure
```

Expected: Debug build succeeds and all tests pass.

- [ ] **Step 3: Perform targeted manual checks**

Verify in Notepad and PowerShell:

1. Last-word mode transforms only the last typed word.
2. All-text mode transforms the full tracked session.
3. A 500-character session uses successful direct deletion.
4. A 501-character session uses successful selection deletion.
5. Enter, navigation, physical click, and window switch reset the session.
6. Script and log list text has padding on all four sides.
7. With suppression enabled, a hotkey does nothing in true fullscreen but still
   works in an ordinary maximized window.

- [ ] **Step 4: Record final evidence**

Capture the final commit hashes, test count, and any manual check that cannot be
automated in the handoff response.
