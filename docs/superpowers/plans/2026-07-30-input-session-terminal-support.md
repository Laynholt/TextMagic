# Input-session Terminal Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform the last physically typed word in ordinary editors and terminal command lines without sending terminal copy/paste shortcuts or classifying terminal processes.

**Architecture:** Bind `InputBuffer` captures to a foreground-window context and mutation generation. A valid last-word capture takes priority over clipboard probing; after PowerShell succeeds, TextMagic revalidates the capture, deletes the original with Backspace, and types the result with the existing Unicode `SendInput` path.

**Tech Stack:** C++17, Win32 low-level keyboard/mouse hooks, `SendInput`, PowerShell, CMake/CTest.

## Global Constraints

- Windows-only; do not add cross-platform abstractions.
- Add no dependency or test framework.
- Terminal support is limited to the current physically typed word.
- Never classify terminals by process name, window class, or IDE.
- Do not redesign custom widgets, popup windows, tooltips, renderers, or context menus.
- Preserve the existing selected-text clipboard path when no valid input session exists.
- UI-triggered script execution remains clipboard-only.
- Ignore injected input in the global hooks.
- Tests must fail through stderr/nonzero exit and must never open MSVC `assert` dialogs.
- Every shell command starts with `rtk`.
- The local root `AGENTS.md` is excluded through `.git/info/exclude` and is never committed.

---

### Task 1: Bind previous-word captures to an input context

**Files:**
- Modify: `src/core/text/InputBuffer.h`
- Modify: `src/core/text/InputBuffer.cpp`
- Test: `tests/InputBufferTests.cpp`

**Interfaces:**
- Produces: `InputBuffer::ContextId`, an opaque `std::uintptr_t`.
- Produces: `AppendText(ContextId, const std::wstring&)`.
- Produces: `PopCharacter(ContextId)`.
- Produces: `TryPeekPreviousWord(ContextId, PreviousWordCapture*) const`.
- Produces: `IsCaptureCurrent(ContextId, const PreviousWordCapture&) const`.
- Produces: `CommitReplacement(ContextId, const PreviousWordCapture&, const std::wstring&)`.
- `PreviousWordCapture` additionally carries `contextId` and `generation`.

- [ ] **Step 1: Write context and generation regression tests**

Replace the existing context-free calls in `tests/InputBufferTests.cpp` and add these checks:

```cpp
constexpr InputBuffer::ContextId editorContext = 1;
constexpr InputBuffer::ContextId terminalContext = 2;

InputBuffer buffer;
buffer.AppendText(editorContext, L"hello world   ");

InputBuffer::PreviousWordCapture capture;
Expect(buffer.TryPeekPreviousWord(editorContext, &capture), "expected editor capture");
Expect(capture.word == L"world", "expected last word");
Expect(capture.trailing == L"   ", "expected trailing separators");
Expect(buffer.IsCaptureCurrent(editorContext, capture), "expected current editor capture");
Expect(!buffer.IsCaptureCurrent(terminalContext, capture), "capture must be bound to its input context");

buffer.AppendText(editorContext, L"again");
Expect(!buffer.IsCaptureCurrent(editorContext, capture), "new input must stale the capture");

buffer.Clear();
buffer.AppendText(editorContext, L"editor ");
Expect(buffer.TryPeekPreviousWord(editorContext, &capture), "expected second editor capture");
buffer.AppendText(terminalContext, L"terminal ");
Expect(buffer.TextForTest() == L"terminal ", "context switch must start a fresh session");
Expect(!buffer.IsCaptureCurrent(editorContext, capture), "old-window capture must be stale");

Expect(buffer.TryPeekPreviousWord(terminalContext, &capture), "expected terminal capture");
Expect(buffer.CommitReplacement(terminalContext, capture, L"TERMINAL "),
       "expected terminal replacement commit");
Expect(buffer.TextForTest() == L"TERMINAL ", "expected committed terminal text");

buffer.Clear();
Expect(!buffer.IsCaptureCurrent(terminalContext, capture), "clear must stale captures");
```

Retain the existing left-drift, trailing-space, Russian/English round-trip, stale-capture, and separator-only assertions, updated to pass a context.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
rtk cmake --build build/codex-final-integrated --config Debug --target TextMagicInputBufferTests
```

Expected: compile failure because the context-aware overloads and capture fields do not exist.

- [ ] **Step 3: Add the minimal context-aware state**

In `src/core/text/InputBuffer.h`, use platform-neutral integer identities so the core class does not include `windows.h`:

```cpp
#include <cstdint>
#include <string>

class InputBuffer {
public:
    using ContextId = std::uintptr_t;

    struct PreviousWordCapture {
        std::wstring word;
        std::wstring trailing;
        size_t deleteChars = 0;
        size_t replaceOffset = 0;
        size_t expectedSize = 0;
        ContextId contextId = 0;
        std::uint64_t generation = 0;
    };

    void Clear();
    void PopCharacter(ContextId contextId);
    void AppendText(ContextId contextId, const std::wstring& text);

    bool TryPeekPreviousWord(ContextId contextId, PreviousWordCapture* capture) const;
    bool IsCaptureCurrent(ContextId contextId, const PreviousWordCapture& capture) const;
    bool CommitReplacement(ContextId contextId,
                           const PreviousWordCapture& capture,
                           const std::wstring& replacement);

    const std::wstring& TextForTest() const { return m_text; }

private:
    static bool IsWordSeparator(wchar_t ch);
    void SwitchContext(ContextId contextId);
    void TrimToLimit();

    std::wstring m_text;
    ContextId m_contextId = 0;
    std::uint64_t m_generation = 0;
};
```

In `src/core/text/InputBuffer.cpp`, make every mutation change the generation and make a context switch discard the prior session:

```cpp
void InputBuffer::SwitchContext(ContextId contextId) {
    if (m_contextId == contextId) {
        return;
    }
    m_text.clear();
    m_contextId = contextId;
    ++m_generation;
}

void InputBuffer::Clear() {
    m_text.clear();
    m_contextId = 0;
    ++m_generation;
}

void InputBuffer::AppendText(ContextId contextId, const std::wstring& text) {
    if (text.empty() || contextId == 0) {
        return;
    }
    SwitchContext(contextId);
    m_text += text;
    ++m_generation;
    TrimToLimit();
}

void InputBuffer::PopCharacter(ContextId contextId) {
    SwitchContext(contextId);
    if (!m_text.empty()) {
        m_text.pop_back();
        ++m_generation;
    }
}
```

`TryPeekPreviousWord` returns false unless `contextId != 0 && contextId == m_contextId`, then stores `contextId` and `m_generation` in the capture. `IsCaptureCurrent` compares context, generation, size, offset, and expected tail. `CommitReplacement` calls that validation, performs the existing replacement, increments `m_generation`, and trims.

- [ ] **Step 4: Run focused GREEN**

Run:

```powershell
rtk cmake --build build/codex-final-integrated --config Debug --target TextMagicInputBufferTests
rtk ctest --test-dir build/codex-final-integrated -C Debug -R TextMagicInputBufferTests --output-on-failure
```

Expected: `TextMagicInputBufferTests` passes without a GUI dialog.

- [ ] **Step 5: Commit the input-session core**

```powershell
rtk git add src/core/text/InputBuffer.h src/core/text/InputBuffer.cpp tests/InputBufferTests.cpp
rtk git commit -m "feat: bind input buffer to active context"
```

---

### Task 2: Use the input session before clipboard probing

**Files:**
- Modify: `src/app/Application.cpp`
- Modify: `src/core/text/TextBridge.h`
- Modify: `src/core/text/TextBridge.cpp`
- Test: `tests/InputBufferTests.cpp` (the Task 1 capture/staleness tests are the automated contract)

**Interfaces:**
- Consumes: all context-aware `InputBuffer` methods from Task 1.
- Consumes: `TextBridge::DeleteCharacters(size_t)`.
- Consumes: `TextBridge::TypeText(const std::wstring&, size_t*)`.
- Removes: `TextBridge::SelectPreviousCharacters` and `TextBridge::CollapseSelection`.
- Preserves: `GetSelectedText`, `SetSelectedText`, `GetAllText`, and `SetAllText`.

- [ ] **Step 1: Record the current real-world RED and static evidence**

Before editing, confirm the old terminal-sensitive path is present:

```powershell
rtk rg -n "GetSelectedText|SelectPreviousCharacters|SetSelectedText\\(mergedText" src/app/Application.cpp
rtk rg -n "SendCtrlShortcut\\('C'\\)" src/core/text/TextBridge.cpp
```

Expected: script hotkeys probe selection before `InputBuffer`, and previous-word replacement uses selection plus clipboard paste. This is the reported failing terminal behavior; automated hook input cannot reproduce it because injected events are intentionally ignored.

- [ ] **Step 2: Pass the foreground context through all buffer wrappers**

Add one conversion helper near the existing input-buffer globals:

```cpp
InputBuffer::ContextId CurrentInputContext() {
    return reinterpret_cast<InputBuffer::ContextId>(GetForegroundWindow());
}
```

Update wrappers to accept or obtain the context explicitly:

```cpp
void PopInputBufferCharacter(InputBuffer::ContextId contextId) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    g_inputBuffer.PopCharacter(contextId);
}

void AppendInputBufferText(InputBuffer::ContextId contextId, const std::wstring& text) {
    if (text.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    g_inputBuffer.AppendText(contextId, text);
}

bool PeekPreviousWordFromInputBuffer(
    InputBuffer::ContextId contextId,
    InputBuffer::PreviousWordCapture* capture
) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    return g_inputBuffer.TryPeekPreviousWord(contextId, capture);
}
```

Make `IsPreviousWordCaptureCurrent` and `CommitPreviousWordReplacement` use `capture.contextId`.

- [ ] **Step 3: Make physical navigation invalidate the session**

Reorder `HandleInputBufferKeyDown` so the registered TextMagic hotkey is checked before clearing, then apply these exact rules:

```cpp
const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
const bool winDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
    || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

if (IsTrackedHotkeyPressed(vkCode, controlDown, altDown, shiftDown, winDown)) {
    return;
}

switch (vkCode) {
case VK_LEFT:
case VK_RIGHT:
case VK_UP:
case VK_DOWN:
case VK_HOME:
case VK_END:
case VK_PRIOR:
case VK_NEXT:
case VK_INSERT:
case VK_DELETE:
case VK_RETURN:
case VK_TAB:
case VK_ESCAPE:
    ClearInputBuffer();
    return;
case VK_BACK:
    if (controlDown || altDown || winDown) {
        ClearInputBuffer();
    } else {
        PopInputBufferCharacter(CurrentInputContext());
    }
    return;
default:
    break;
}

if (controlDown || altDown || winDown) {
    ClearInputBuffer();
    return;
}
```

Keep modifier-only keys, Caps Lock, and F1–F24 ignored. Pass `CurrentInputContext()` into `AppendInputBufferText` after successful `ToUnicodeEx`.

- [ ] **Step 4: Resolve a valid last word before clipboard selection**

Capture the foreground context in `Application::ExecuteScript` before starting the worker thread:

```cpp
const InputBuffer::ContextId inputContext = CurrentInputContext();
```

Capture it in the lambda. For non-clipboard UI execution, resolve in this order:

```cpp
if (PeekPreviousWordFromInputBuffer(inputContext, &previousWordCapture)
    && !previousWordCapture.word.empty()) {
    sourceText = previousWordCapture.word;
    previousWordMode = true;
} else {
    selectedText = textBridge.GetSelectedText();
    hasSelection = !selectedText.empty();
    sourceText = hasSelection ? selectedText : L"";

    if (!hasSelection && fallbackToAllText) {
        sourceText = textBridge.GetAllText();
    }
}
```

Delete the redundant `ScriptExecutionTaskResult::previousWordTrailing` and local `previousWordTrailing`; use `previousWordCapture.trailing`.

- [ ] **Step 5: Replace the current word without selection or clipboard paste**

Replace the previous-word completion branch with a delete/type transaction:

```cpp
const auto& capture = result->previousWordCapture;
const std::wstring replacement = result->outputText + capture.trailing;

if (IsPreviousWordCaptureCurrent(capture)
    && m_textBridge.DeleteCharacters(capture.deleteChars)) {
    size_t typedChars = 0;
    if (m_textBridge.TypeText(replacement, &typedChars)) {
        replaceOk = CommitPreviousWordReplacement(capture, replacement);
        if (!replaceOk) {
            ClearInputBuffer();
        }
    } else {
        if (typedChars > 0) {
            m_textBridge.DeleteCharacters(typedChars);
        }
        size_t restoredChars = 0;
        const bool restored = m_textBridge.TypeText(
            capture.word + capture.trailing,
            &restoredChars
        );
        if (!restored) {
            ClearInputBuffer();
        }
    }
}
```

Do not delete the source word before PowerShell completes. Preserve the existing status/log/dialog behavior for a failed replacement.

- [ ] **Step 6: Delete replacement helpers that no longer have callers**

Remove these declarations and definitions:

```cpp
bool TextBridge::SelectPreviousCharacters(size_t count) const;
bool TextBridge::CollapseSelection() const;
bool TextBridge::SendRepeatedShiftKey(WORD virtualKey, size_t count) const;
```

Confirm no references remain:

```powershell
rtk rg -n "SelectPreviousCharacters|CollapseSelection|SendRepeatedShiftKey" src tests
```

Expected: no matches.

- [ ] **Step 7: Build and run the complete automated suite**

Run:

```powershell
rtk cmake -S . -B build/codex-input-session
rtk cmake --build build/codex-input-session --config Debug --parallel
rtk ctest --test-dir build/codex-input-session -C Debug --output-on-failure
rtk git diff --check
```

Expected: configure/build succeed, all tests pass, and the diff check is clean.

- [ ] **Step 8: Commit terminal-safe previous-word execution**

```powershell
rtk git add src/app/Application.cpp src/core/text/TextBridge.h src/core/text/TextBridge.cpp
rtk git commit -m "fix: replace previous word without clipboard shortcuts"
```

---

### Task 3: Document the product contract and perform physical smoke tests

**Files:**
- Modify: `README.md`
- Create locally only: `AGENTS.md`
- Modify locally only: `.git/info/exclude`

**Interfaces:**
- Documents the Task 2 behavior; produces no code API.
- The local `AGENTS.md` is persistent Codex context for this workspace and is not part of Git history.

- [ ] **Step 1: Update the tracked README**

Correct the source-selection section and include this contract:

```markdown
### Источник текста

При запуске скрипта по горячей клавише TextMagic использует:

1. последнее физически введённое слово, если пользователь не перемещал каретку и не менял окно;
2. явно выделенный текст, если сессия последнего слова была сброшена;
3. весь текст — только при включённом соответствующем fallback-режиме.

В терминалах поддерживается преобразование текущего введённого слова.
TextMagic не изменяет историю терминала и не пытается копировать её через `Ctrl+C`.
```

Retain the existing `.tmscript` format, UTF-8 stdin/stdout protocol, UI description, and source-tree summary.

- [ ] **Step 2: Create local Codex context without changing `.gitignore`**

Add this exact line to `.git/info/exclude` if absent:

```text
/AGENTS.md
```

Create root `AGENTS.md` with:

```markdown
# Local TextMagic context

- TextMagic is a Windows-only native text-transformation hub.
- It operates on text in other applications: explicit selections or the last physically typed word.
- PowerShell `.tmscript` files receive one UTF-8 string on stdin and return one UTF-8 string on stdout.
- Terminal support is intentionally limited to the current typed word; terminal history is not editable.
- Do not classify terminals by process name. Input-session validity drives last-word behavior.
- Custom widgets, popup windows, renderers, tooltips, and context menus are intentional UI requirements, not accidental over-engineering.
- Read `README.md` and `docs/superpowers/specs/2026-07-30-input-session-terminal-support-design.md` before changing input behavior.
```

Verify it is ignored:

```powershell
rtk git check-ignore -v AGENTS.md
```

Expected: `.git/info/exclude` is the matching rule.

- [ ] **Step 3: Run automated verification again**

Run:

```powershell
rtk cmake --build build/codex-input-session --config Debug --parallel
rtk ctest --test-dir build/codex-input-session -C Debug --output-on-failure
rtk git diff --check
```

Expected: all tests pass and the diff is clean.

- [ ] **Step 4: Commit only tracked documentation**

```powershell
rtk git add README.md
rtk git commit -m "docs: describe input-session text handling"
```

Confirm `AGENTS.md` is not staged:

```powershell
rtk git status --short
```

- [ ] **Step 5: Perform the physical-keyboard smoke matrix**

Launch `build/codex-input-session/bin/Debug/TextMagic.exe`. The user performs these cases with physical keys:

1. Notepad: type `abc hello `, run Uppercase, expect `abc HELLO `.
2. Notepad: select text with the mouse, run Uppercase, expect only the selection to change.
3. Notepad: type text, press `Ctrl+A`, run Uppercase, expect the full selection to change.
4. Windows Terminal PowerShell: type `abc hello ` without submitting, run Uppercase, expect `abc HELLO ` and no command cancellation.
5. Windows Terminal cmd: repeat case 4.
6. VS Code editor: repeat cases 1 and 2.
7. VS Code integrated terminal: repeat case 4.
8. Start a deliberately slow script, then type or switch windows before it completes; expect no replacement.
9. Run the RU/EN layout script twice on the last word; expect no characters to the left of the word to be changed.

Record every case as PASS/FAIL. Any failure returns to systematic debugging with the exact application, keys, before-text, after-text, and TextMagic log entry; do not add application-specific exceptions without a new design decision.

