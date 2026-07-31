# Reliable Text Replacement and Hotkey Exclusions Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix tracked-text replacement in editors, lower the direct-Backspace threshold to 100 characters, and move the fullscreen setting into a modeless exclusions window.

**Architecture:** Keep replacement routed through `TextBridge` and source selection routed through `ScriptInputSource`. Reuse the existing `InfoWindowProc` window class for the single new exclusions window; do not add a new UI subsystem or future blacklist data model.

**Tech Stack:** C++17, Win32 `SendInput`, existing Win32 window classes, CMake/CTest.

## Global Constraints

- Use repeated Backspace for captures up to and including 100 UTF-16 code units.
- Use incremental Shift+Left selection for captures longer than 100 units.
- Valid tracked input wins before the Ctrl+C selection probe.
- Never type transformed text after deletion or selection failure.
- Do not use the clipboard for tracked-text deletion or insertion.
- The exclusions window is modeless, single-instance, and persists checkbox changes immediately.
- Do not add process lists, executable pickers, or blacklist storage.
- Do not touch `.codebase-memory/`.

---

### Task 1: Prefer valid tracked input over the selection probe

**Files:**
- Modify: `src/app/ScriptInputSource.h`
- Modify: `src/app/Application.cpp`
- Test: `tests/ScriptInputSourceTests.cpp`

**Interfaces:**
- Consumes: `ScriptInputSource::Choose(bool clipboardOnly, bool hasSelection, bool hasTrackedInput)`
- Produces: the same function and enum, with tracked input selected before selection when both are available.

- [ ] **Step 1: Change the focused test to describe editor-safe precedence**

Add assertions equivalent to:

```cpp
Expect(Choose(false, true, true) == Type::TrackedInput,
       "valid tracked input must avoid false editor selections");
Expect(Choose(false, true, false) == Type::Selection,
       "selection remains the fallback without tracked input");
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicScriptInputSourceTests
rtk ctest --test-dir build -C Debug -R TextMagicScriptInputSourceTests --output-on-failure
```

Expected: the first assertion fails because `Selection` currently wins.

- [ ] **Step 3: Implement the smallest precedence change**

Change `Choose` to:

```cpp
constexpr Type Choose(bool clipboardOnly, bool hasSelection, bool hasTrackedInput) noexcept {
    return clipboardOnly
        ? Type::Clipboard
        : (hasTrackedInput
            ? Type::TrackedInput
            : (hasSelection ? Type::Selection : Type::None));
}
```

In `Application::ExecuteScript`, avoid calling `GetSelectedText()` when
`hasInputCapture` is already true. This removes the destructive/unreliable
Ctrl+C probe from the tracked-input path:

```cpp
if (!useClipboardOnly && !hasInputCapture) {
    selectedText = textBridge.GetSelectedText();
    hasSelection = !selectedText.empty();
}
```

- [ ] **Step 4: Verify GREEN**

Run the focused test again. Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
rtk git add -- src/app/ScriptInputSource.h src/app/Application.cpp tests/ScriptInputSourceTests.cpp
rtk git commit -m "fix: prefer tracked text in editors"
```

---

### Task 2: Select long tracked text while Shift remains active

**Files:**
- Modify: `src/core/text/TextBridge.h`
- Modify: `src/core/text/TextBridge.cpp`
- Modify: `src/core/text/TextBridgeInputUtils.h`
- Test: `tests/TextBridgeInputUtilsTests.cpp`

**Interfaces:**
- Consumes: `TextBridge::DeleteCharacters(size_t count)` and `TextBridge::SelectPreviousCharacters(size_t count)`
- Produces: `TextBridgeInputUtils::ShouldSelectBeforeDelete(size_t count)` with a fixed 100-unit boundary.

- [ ] **Step 1: Add failing 100/101 boundary checks**

Add:

```cpp
using TextBridgeInputUtils::ShouldSelectBeforeDelete;
Expect(!ShouldSelectBeforeDelete(100),
       "100 characters must use direct Backspace");
Expect(ShouldSelectBeforeDelete(101),
       "101 characters must use selection");
```

Remove `SelectionCleanupPlan`, `PlanPartialSelectionCleanup`, and their tests;
the incremental implementation no longer builds or partially submits one
combined input array.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicTextBridgeInputUtilsTests
rtk ctest --test-dir build -C Debug -R TextMagicTextBridgeInputUtilsTests --output-on-failure
```

Expected: compilation fails because `ShouldSelectBeforeDelete` is not exported.

- [ ] **Step 3: Add the fixed strategy boundary**

In `TextBridgeInputUtils.h`:

```cpp
constexpr size_t DIRECT_BACKSPACE_LIMIT = 100;

constexpr bool ShouldSelectBeforeDelete(size_t count) noexcept {
    return count > DIRECT_BACKSPACE_LIMIT;
}
```

Use this helper from `DeleteCharacters`.

- [ ] **Step 4: Replace the single giant selection batch**

Implement `SelectPreviousCharacters` with this minimal sequence:

1. Send Shift-down alone.
2. Send each Left down/up pair while Shift remains down.
3. Yield briefly after small chunks so the target thread can process the input
   before Shift-up.
4. Release Shift.
5. On any failed Left send, release Shift, collapse any created selection with
   Right, and return `false`.

Keep the chunk size and delay as internal constants, not settings. Do not build
one vector containing Shift-up together with all Left events.

Illustrative core:

```cpp
if (!SendKeyDown(VK_SHIFT)) {
    return false;
}

size_t selected = 0;
for (; selected < count; ++selected) {
    if (!SendKey(VK_LEFT)) {
        SendKeyUp(VK_SHIFT);
        if (selected > 0) {
            SendKey(VK_RIGHT);
        }
        return false;
    }
    if ((selected + 1) % 32 == 0) {
        Sleep(1);
    }
}

Sleep(10);
if (!SendKeyUp(VK_SHIFT)) {
    SendKeyUp(VK_SHIFT);
    SendKey(VK_RIGHT);
    return false;
}
return true;
```

Add the private `SendKeyDown(WORD)` counterpart to the existing `SendKeyUp`.

- [ ] **Step 5: Verify GREEN and the existing cleanup behavior**

Run the focused test. Expected: PASS.

Then run:

```powershell
rtk ctest --test-dir build -C Debug -R "TextMagic(InputBuffer|TextBridgeInputUtils|ScriptInputSource)Tests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit**

```powershell
rtk git add -- src/core/text/TextBridge.h src/core/text/TextBridge.cpp src/core/text/TextBridgeInputUtils.h tests/TextBridgeInputUtilsTests.cpp
rtk git commit -m "fix: pace long text selection"
```

---

### Task 3: Move fullscreen suppression into an exclusions window

**Files:**
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`
- Test: `tests/LocalizationTests.cpp`

**Interfaces:**
- Produces: `Application::ShowHotkeyExclusionsWindow()` and one stored
  `m_hHotkeyExclusionsWindow` handle.
- Reuses: `InfoWindowProc`, `InfoWindowState`, `SaveDisableFullscreenHotkeysSetting`.

- [ ] **Step 1: Add localization expectations**

Require these keys in both language files:

```ini
menu.hotkey_exclusions=Hotkey exclusions...
hotkey_exclusions.disable_fullscreen=Disable hotkeys in fullscreen applications
```

and:

```ini
menu.hotkey_exclusions=Исключения горячих клавиш...
hotkey_exclusions.disable_fullscreen=Отключать горячие клавиши в полноэкранных приложениях
```

Update `LocalizationTests.cpp` using its existing key-checking pattern.

- [ ] **Step 2: Run localization tests and verify RED**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicLocalizationTests
rtk ctest --test-dir build -C Debug -R TextMagicLocalizationTests --output-on-failure
```

Expected: missing-key assertion fails.

- [ ] **Step 3: Add the exclusions-window kind and controls**

Extend the existing info-window state rather than creating another window
framework:

```cpp
enum class InfoWindowKind {
    About = 1,
    Logs = 2,
    HotkeyExclusions = 3
};
```

Add one checkbox handle to `InfoWindowState`, one checkbox control ID, and
`m_hHotkeyExclusionsWindow` to `Application`.

For `HotkeyExclusions`, `InfoWindowProc::WM_CREATE` creates:

```cpp
CreateWindowExW(
    0, L"BUTTON", T(L"hotkey_exclusions.disable_fullscreen"),
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
    0, 0, 100, 28,
    hWnd,
    reinterpret_cast<HMENU>(ID_INFO_FULLSCREEN_CHECKBOX),
    GetModuleHandleW(nullptr),
    nullptr
);
```

Initialize its check state from `m_disableHotkeysInFullscreen`. On
`BN_CLICKED`, update the member and call the existing save function
immediately.

- [ ] **Step 4: Replace the checked menu item with an opener**

Rename `ID_MENU_DISABLE_HOTKEYS_FULLSCREEN` to
`ID_MENU_HOTKEY_EXCLUSIONS`, map it to `menu.hotkey_exclusions`, render it
unchecked, and change its command handler to `ShowHotkeyExclusionsWindow()`.

`ShowHotkeyExclusionsWindow()` reuses `CreateOrActivateInfoWindow`; repeated
calls activate the existing modeless window. Closing it clears only
`m_hHotkeyExclusionsWindow`.

- [ ] **Step 5: Complete layout and localization**

Give the exclusions window a compact fixed initial size, position the checkbox
and existing Close button in `WM_SIZE`, and apply the existing application font.
Update open info windows when localization changes.

- [ ] **Step 6: Verify GREEN**

Run the focused localization test and the Debug build. Expected: both pass.

- [ ] **Step 7: Commit**

```powershell
rtk git add -- src/app/Application.h src/app/Application.cpp lang/en.ini lang/ru.ini tests/LocalizationTests.cpp
rtk git commit -m "feat: add hotkey exclusions window"
```

---

### Task 4: Final verification

**Files:**
- Verify only.

**Interfaces:**
- Consumes the completed tree.
- Produces fresh build, test, diff, and status evidence.

- [ ] **Step 1: Build**

```powershell
rtk cmake --build build --config Debug
```

Expected: all application and test targets build successfully.

- [ ] **Step 2: Run all tests**

```powershell
rtk ctest --test-dir build -C Debug --output-on-failure
```

Expected: 100% tests pass.

- [ ] **Step 3: Check the tree**

```powershell
rtk git diff --check
rtk git status --short --branch
```

Expected: no whitespace errors and only the pre-existing untracked
`.codebase-memory/`.

- [ ] **Step 4: Manual checks when desktop automation works**

- Type 100 characters in Notepad and apply a script: old text is removed once.
- Type 101 and more characters in Notepad and Sublime Text: the full capture is
  selected, deleted once, and replaced once.
- Open “Hotkey exclusions…” repeatedly: one window is reused.
- Toggle the checkbox, close/reopen the window, and confirm persistence.
- Confirm ordinary maximized applications still receive TextMagic hotkeys and
  true fullscreen applications do not when the checkbox is enabled.
