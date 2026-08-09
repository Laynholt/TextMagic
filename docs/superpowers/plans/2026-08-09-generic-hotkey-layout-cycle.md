# Generic Hotkey Layout Cycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a manifest-configured built-in action that cycles installed keyboard layouts and can be bound to any conventional chord, single modifier, modifier-only chord, or repeated modifier without hardcoding Shift in the application.

**Architecture:** Extend `ScriptManifest` with explicit action and hotkey kinds, then route modifier-only bindings through a pure `ModifierGestureResolver`. Conventional chords keep the current immediate hook path. The layout action bypasses text execution and uses a pure next-layout selector plus the existing Win32 application layer.

**Tech Stack:** C++17, Win32 low-level keyboard/mouse hooks, Win32 timers and keyboard-layout APIs, CMake/CTest, existing assert-style executable tests.

## Global Constraints

- The hotkey is owned entirely by each `.tmscript` manifest; `cycle_keyboard_layout` must contain no Shift-specific binding.
- A modifier gesture must be released within 300 ms of its first modifier press.
- A completed modifier gesture resolves at 350 ms from its first modifier press.
- Unrelated keyboard input, a mouse button, a foreground-window change, or a hold longer than 300 ms cancels the gesture.
- A repeated-modifier binding wins over a pending single-modifier binding; a larger modifier chord wins over its shorter prefix.
- Conventional chords containing a non-modifier primary key keep immediate dispatch behavior.
- Layouts follow `GetKeyboardLayoutList` order and wrap from the last layout to the first.
- Built-in actions must not capture, select, delete, paste, or rewrite text and must not launch PowerShell.
- Preserve the existing `Shift+Shift` conversion behavior and every script without `action=cycle_keyboard_layout`.
- Add no dependency and do not refactor unrelated UI or script-runner code.
- Do not stage or modify the user's untracked `scripts/sample_lowercase.tmscript`.

---

## File Map

- `src/core/scripts/ScriptManifest.h`: manifest action and normalized hotkey-kind data.
- `src/core/scripts/ScriptManifest.cpp`: action validation and generic hotkey grammar.
- `src/app/ModifierGestureResolver.h`: pure modifier-gesture state machine.
- `src/app/OutputLayout.h`: pure next-installed-layout selection.
- `src/app/Application.cpp`: hook/timer integration and Win32 layout request.
- `src/app/ScriptExecutionGate.h`: small pure keyboard-state overlay used by hook input capture.
- `src/app/Application.h`: built-in action execution declarations.
- `scripts/cycle_keyboard_layout.tmscript`: bundled manifest-only action with the default `Shift` binding.
- `lang/en.ini`, `lang/ru.ini`: manifest and layout-action status text.
- `tests/ScriptManifestTests.cpp`: manifest and grammar regression coverage.
- `tests/ModifierGestureResolverTests.cpp`: deterministic gesture sequences without real hooks.
- `tests/OutputLayoutTests.cpp`: cycle and wraparound coverage.
- `tests/LocalizationTests.cpp`: embedded localization key coverage.
- `CMakeLists.txt`: the new pure resolver test target.

---

### Task 1: Preserve hook-reported Shift state in captured text

**Files:**
- Modify: `src/app/ScriptExecutionGate.h`
- Modify: `src/app/Application.cpp`
- Modify: `tests/ScriptExecutionGateTests.cpp`

**Interfaces:**
- Consumes: `currentModifiers` already computed by `InputKeyboardHookProc` and passed to `HandleInputBufferKeyDown`.
- Produces: `HotkeyDispatch::ApplyHookShiftState(BYTE*, bool) noexcept` and `AppendKeyToInputBuffer(DWORD, DWORD, UINT)`.
- Keeps: Caps Lock toggle state from `GetKeyboardState` and the current `ToUnicodeEx` conversion path.

- [ ] **Step 1: Add the failing keyboard-state regression**

Extend `tests/ScriptExecutionGateTests.cpp`:

```cpp
BYTE keyboardState[256] = {};
keyboardState[VK_SHIFT] = 0x01;
HotkeyDispatch::ApplyHookShiftState(keyboardState, true);
Expect((keyboardState[VK_SHIFT] & 0x80) != 0,
       "hook Shift-down must reach ToUnicodeEx state");
Expect((keyboardState[VK_SHIFT] & 0x01) != 0,
       "Shift overlay must preserve existing toggle bits");

HotkeyDispatch::ApplyHookShiftState(keyboardState, false);
Expect((keyboardState[VK_SHIFT] & 0x80) == 0,
       "hook Shift-up must clear stale GetKeyboardState state");
```

- [ ] **Step 2: Run the focused test and confirm it fails to compile**

```powershell
rtk cmake --build build --config Release --target TextMagicScriptExecutionGateTests
```

Expected: compilation fails because `ApplyHookShiftState` is undefined.

- [ ] **Step 3: Add the minimal pure overlay**

Add inside `namespace HotkeyDispatch` in `ScriptExecutionGate.h`:

```cpp
inline void ApplyHookShiftState(BYTE* keyboardState, bool shiftDown) noexcept {
    if (!keyboardState) {
        return;
    }
    keyboardState[VK_SHIFT] = static_cast<BYTE>(
        (keyboardState[VK_SHIFT] & 0x7f) | (shiftDown ? 0x80 : 0));
}
```

Do not synthesize Caps Lock; retain its toggle bit from `GetKeyboardState`.

- [ ] **Step 4: Pass authoritative modifiers into character conversion**

Change the helper signature and its only call:

```cpp
void AppendKeyToInputBuffer(DWORD vkCode, DWORD scanCode, UINT currentModifiers) {
    BYTE keyboardState[256] = {};
    if (!GetKeyboardState(keyboardState)) {
        return;
    }
    HotkeyDispatch::ApplyHookShiftState(
        keyboardState,
        (currentModifiers & MOD_SHIFT) != 0);
    // existing key-down bit, layout lookup, and ToUnicodeEx code
}

// HandleInputBufferKeyDown:
AppendKeyToInputBuffer(vkCode, scanCode, currentModifiers);
```

- [ ] **Step 5: Run the case regressions**

```powershell
rtk cmake --build build --config Release --target TextMagicScriptExecutionGateTests TextMagicScriptRunnerTests TextMagic
rtk ctest --test-dir build -C Release -R "TextMagic(ScriptExecutionGate|ScriptRunner)Tests" --output-on-failure
```

Expected: hook-state checks pass, and direct conversions still cover `ghbdtn`, `Ghbdtn`, and `GHBDTN` in both directions.

- [ ] **Step 6: Commit Task 1**

```powershell
rtk git add src/app/ScriptExecutionGate.h src/app/Application.cpp tests/ScriptExecutionGateTests.cpp
rtk git commit -m "fix: preserve captured input case"
```

---

### Task 2: Parse built-in actions and generic hotkey forms

**Files:**
- Modify: `src/core/scripts/ScriptManifest.h`
- Modify: `src/core/scripts/ScriptManifest.cpp`
- Modify: `tests/ScriptManifestTests.cpp`
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`
- Modify: `tests/LocalizationTests.cpp`

**Interfaces:**
- Consumes: the current `.tmscript` header reader and `ParseVirtualKey` helper.
- Produces: `ScriptManifest::Action`, `ScriptManifest::HotkeyKind`, `Entry::action`, `Entry::hotkeyKind`, and `ParseHotkey(const std::wstring&, HotkeyKind*, UINT*, UINT*, std::wstring*)`.
- Keeps: `Entry::modifiers` and `Entry::virtualKey`, so existing application code continues compiling until runtime integration.

- [ ] **Step 1: Add failing parser and loader checks**

Extend `tests/ScriptManifestTests.cpp` with a direct parser helper and temporary action manifests:

```cpp
void CheckHotkey(
    const wchar_t* text,
    ScriptManifest::HotkeyKind expectedKind,
    UINT expectedModifiers,
    UINT expectedVirtualKey
) {
    ScriptManifest::HotkeyKind kind = ScriptManifest::HotkeyKind::KeyChord;
    UINT modifiers = 0;
    UINT virtualKey = 0;
    std::wstring error;
    const bool parsed = ScriptManifest::ParseHotkey(
        text, &kind, &modifiers, &virtualKey, &error);
    Check(parsed, "hotkey form parses");
    Check(kind == expectedKind, "hotkey kind is normalized");
    Check(modifiers == expectedModifiers, "modifier mask is normalized");
    Check(virtualKey == expectedVirtualKey, "primary key is normalized");
}

CheckHotkey(L"Shift", ScriptManifest::HotkeyKind::ModifierGesture,
            MOD_SHIFT, VK_SHIFT);
CheckHotkey(L"Ctrl", ScriptManifest::HotkeyKind::ModifierGesture,
            MOD_CONTROL, VK_CONTROL);
CheckHotkey(L"Ctrl+Shift", ScriptManifest::HotkeyKind::ModifierGesture,
            MOD_CONTROL | MOD_SHIFT, 0);
CheckHotkey(L"Shift+Ctrl", ScriptManifest::HotkeyKind::ModifierGesture,
            MOD_CONTROL | MOD_SHIFT, 0);
CheckHotkey(L"Shift+Shift", ScriptManifest::HotkeyKind::ModifierDoubleTap,
            MOD_SHIFT, VK_SHIFT);
CheckHotkey(L"Ctrl+Alt+L", ScriptManifest::HotkeyKind::KeyChord,
            MOD_CONTROL | MOD_ALT, 'L');
```

Create `cycle.tmscript` with `name`, `hotkey=Shift`, `action=cycle_keyboard_layout`, and `enabled=true`, but no command/body. Create `unknown-action.tmscript` with `action=unknown`. Assert the cycle entry loads with `Action::CycleKeyboardLayout`, the unknown entry is absent, and the warning names its unsupported action.

- [ ] **Step 2: Run the manifest target and confirm the expected failure**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicScriptManifestTests
```

Expected: compilation fails because `Action`, `HotkeyKind`, and the expanded `ParseHotkey` signature do not exist.

- [ ] **Step 3: Add the normalized manifest types**

Add these nested enums and defaults in `ScriptManifest.h`:

```cpp
enum class Action {
    TransformText,
    CycleKeyboardLayout,
};

enum class HotkeyKind {
    KeyChord,
    ModifierGesture,
    ModifierDoubleTap,
};

struct Entry {
    // existing strings
    UINT modifiers = 0;
    UINT virtualKey = 0;
    Action action = Action::TransformText;
    HotkeyKind hotkeyKind = HotkeyKind::KeyChord;
    bool enabled = true;
    bool autoOutputLayout = false;
};

static bool ParseHotkey(
    const std::wstring& hotkeyText,
    HotkeyKind* kind,
    UINT* modifiers,
    UINT* virtualKey,
    std::wstring* error);
```

Do not introduce a polymorphic action class or a separate parser object.

- [ ] **Step 4: Implement the minimal hotkey grammar**

Replace the current “first non-modifier primary” loop with one pass that counts modifier tokens and at most one non-modifier token. Normalize modifier aliases to these pairs:

```cpp
struct ModifierToken {
    UINT mask;
    UINT virtualKey;
};

// CTRL/CONTROL -> { MOD_CONTROL, VK_CONTROL }
// ALT          -> { MOD_ALT, VK_MENU }
// SHIFT        -> { MOD_SHIFT, VK_SHIFT }
// WIN/WINDOWS  -> { MOD_WIN, VK_LWIN }
```

Classify the result exactly as follows:

```cpp
if (nonModifierKeyFound) {
    *kind = HotkeyKind::KeyChord;
    *virtualKey = nonModifierVirtualKey;
    return *modifiers != 0;
}
if (tokenCount == 1) {
    *kind = HotkeyKind::ModifierGesture;
    *virtualKey = onlyModifierVirtualKey;
    return true;
}
if (tokenCount == 2 && firstModifierMask == secondModifierMask) {
    *kind = HotkeyKind::ModifierDoubleTap;
    *virtualKey = firstModifierVirtualKey;
    return true;
}
if (allModifierMasksAreDistinct) {
    *kind = HotkeyKind::ModifierGesture;
    *virtualKey = 0;
    return true;
}
```

Reject multiple non-modifier keys, three or more repeated copies of one modifier, and mixed duplicate modifier chords. Preserve the existing rule that a conventional primary key requires at least one modifier.

- [ ] **Step 5: Parse and validate `action`**

In `LoadFromDirectory`, normalize `fields[L"ACTION"]` with `ToUpperAscii(Trim(...))`:

```cpp
const std::wstring actionText = ToUpperAscii(Trim(fields[L"ACTION"]));
if (actionText.empty()) {
    manifest.action = ScriptManifest::Action::TransformText;
} else if (actionText == L"CYCLE_KEYBOARD_LAYOUT") {
    manifest.action = ScriptManifest::Action::CycleKeyboardLayout;
} else {
    warnings << T(L"manifest.warning.parse_skip_prefix") << path.filename().wstring()
             << L": " << T(L"manifest.error.unknown_action_prefix")
             << fields[L"ACTION"] << L"\n";
    continue;
}
```

Require command/body only for `TransformText`:

```cpp
const bool hasExecutable = hasInlineScript || hasCommandLine;
const bool hasRequiredFields = !manifest.name.empty()
    && !manifest.hotkeyText.empty()
    && (manifest.action != ScriptManifest::Action::TransformText || hasExecutable);
```

Pass `&manifest.hotkeyKind` into the new `ParseHotkey` signature.

- [ ] **Step 6: Add localized validation text and checks**

Add the same keys to both `lang/en.ini` and `lang/ru.ini`:

```ini
manifest.error.unknown_action_prefix=Unsupported script action: 
manifest.warning.required_fields=: name/hotkey and script-body, command, or supported action fields are required.\n
```

Use the Russian equivalents in `ru.ini`. Extend `TestEmbeddedLanguagesContainApplicationBlacklistKeys` or add `TestEmbeddedLanguagesContainScriptActionKeys` in `tests/LocalizationTests.cpp` to assert both languages return non-empty `manifest.error.unknown_action_prefix` values.

- [ ] **Step 7: Run focused tests**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagicScriptManifestTests TextMagicLocalizationTests
rtk ctest --test-dir build -C Release -R "TextMagic(ScriptManifest|Localization)Tests" --output-on-failure
```

Expected: both tests pass, and the application still compiles with the preserved flat `modifiers`/`virtualKey` fields.

- [ ] **Step 8: Commit Task 1**

```powershell
rtk git add src/core/scripts/ScriptManifest.h src/core/scripts/ScriptManifest.cpp tests/ScriptManifestTests.cpp lang/en.ini lang/ru.ini tests/LocalizationTests.cpp
rtk git commit -m "feat: parse generic modifier hotkeys"
```

---

### Task 3: Add the pure modifier-gesture resolver

**Files:**
- Create: `src/app/ModifierGestureResolver.h`
- Create: `tests/ModifierGestureResolverTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ScriptManifest::HotkeyKind` and normalized modifier masks from Task 2.
- Produces: `ModifierGestureResolver::Binding`, `ModifierGestureResolver::Decision`, `SetBindings`, `OnKeyDown`, `OnKeyUp`, `OnTimeout`, `Cancel`, `HasPending`, and `DueTick`.
- Does not call Win32 APIs, post messages, read global key state, or own a timer.

- [ ] **Step 1: Create the failing resolver test**

Create `tests/ModifierGestureResolverTests.cpp` with a small `Expect` helper and these bindings:

```cpp
ModifierGestureResolver resolver;
resolver.SetBindings({
    { 1, ScriptManifest::HotkeyKind::ModifierGesture, MOD_SHIFT, VK_SHIFT },
    { 2, ScriptManifest::HotkeyKind::ModifierDoubleTap, MOD_SHIFT, VK_SHIFT },
    { 3, ScriptManifest::HotkeyKind::ModifierGesture, MOD_CONTROL, VK_CONTROL },
    { 4, ScriptManifest::HotkeyKind::ModifierGesture, MOD_CONTROL | MOD_SHIFT, 0 },
});
```

Use context IDs `100` and `200`. Cover these exact sequences:

```cpp
// Single Shift: down 0, up 50, timeout 349 -> none, timeout 350 -> id 1.
// Long Shift: down 1000, up 1301 -> canceled.
// Shift+A: Shift down, A down -> canceled; timeout -> none.
// Mouse: Shift down/up, Cancel(), timeout -> none.
// Foreground change: Shift down/up in 100, timeout in 200 -> none.
// Shift+Shift: first down/up, second down at +200 -> id 2 only.
// Ctrl+Shift: Ctrl down, Shift down, both released -> id 4, never id 3.
// Held repeats: repeated key-down for an already pressed modifier -> no new candidate.
```

Assert modifier decisions never request hook consumption.

- [ ] **Step 2: Register the test target and verify it fails**

Add:

```cmake
add_executable(TextMagicModifierGestureResolverTests
    tests/ModifierGestureResolverTests.cpp
)

target_include_directories(TextMagicModifierGestureResolverTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
)

target_compile_features(TextMagicModifierGestureResolverTests PRIVATE cxx_std_17)
add_test(NAME TextMagicModifierGestureResolverTests COMMAND TextMagicModifierGestureResolverTests)
```

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Release --target TextMagicModifierGestureResolverTests
```

Expected: compilation fails because `ModifierGestureResolver.h` does not exist.

- [ ] **Step 3: Define the minimal resolver interface**

Create a header-only C++17 class:

```cpp
class ModifierGestureResolver {
public:
    static constexpr ULONGLONG MaxHoldMs = 300;
    static constexpr ULONGLONG ResolveMs = 350;

    struct Binding {
        int hotkeyId = 0;
        ScriptManifest::HotkeyKind kind = ScriptManifest::HotkeyKind::ModifierGesture;
        UINT modifiers = 0;
        UINT virtualKey = 0;
    };

    struct Decision {
        int hotkeyId = 0;
        bool pending = false;
        ULONGLONG dueTick = 0;
    };

    void SetBindings(std::vector<Binding> bindings);
    Decision OnKeyDown(DWORD virtualKey, UINT currentModifiers,
                       ULONGLONG now, std::uintptr_t context);
    Decision OnKeyUp(DWORD virtualKey, UINT currentModifiers,
                     ULONGLONG now, std::uintptr_t context);
    Decision OnTimeout(ULONGLONG now, std::uintptr_t context);
    Decision Cancel() noexcept;
    bool HasPending() const noexcept;
    ULONGLONG DueTick() const noexcept;
};
```

Keep storage to the registered bindings, currently pressed modifier mask, one active candidate, one completed pending candidate, and the first-tap data needed by a registered repeated-modifier binding.

- [ ] **Step 4: Implement generic matching and precedence**

Use a side-insensitive modifier-mask helper for Shift, Ctrl, Alt, and Win. On key-down:

1. A non-modifier calls `Cancel()`.
2. Ignore a key-down whose modifier bit is already present in the resolver's pressed mask.
3. If a repeated-modifier binding matches a completed first tap within `ResolveMs`, clear the shorter pending candidate and return its ID immediately.
4. Otherwise, choose the enabled `ModifierGesture` binding whose mask equals `currentModifiers`; prefer the mask with the most set bits and preserve the original first-press tick when upgrading `Ctrl` to `Ctrl+Shift`.

On key-up, remove the released bit. When all bits from the active candidate are released:

```cpp
if (now - m_startedTick > MaxHoldMs || context != m_context) {
    return Cancel();
}
m_completed = true;
m_dueTick = m_startedTick + ResolveMs;
return { 0, true, m_dueTick };
```

`OnTimeout` dispatches only when the candidate is completed, `now >= dueTick`, and the context still matches. It then clears all pending state. `Cancel` clears active, completed, and first-tap state and returns `{0, false, 0}`.

- [ ] **Step 5: Run the resolver checks**

```powershell
rtk cmake --build build --config Release --target TextMagicModifierGestureResolverTests
rtk ctest --test-dir build -C Release -R TextMagicModifierGestureResolverTests --output-on-failure
```

Expected: all deterministic event sequences pass.

- [ ] **Step 6: Commit Task 2**

```powershell
rtk git add CMakeLists.txt src/app/ModifierGestureResolver.h tests/ModifierGestureResolverTests.cpp
rtk git commit -m "feat: resolve generic modifier gestures"
```

---

### Task 4: Add layout cycling as a built-in script action

**Files:**
- Modify: `src/app/OutputLayout.h`
- Modify: `tests/OutputLayoutTests.cpp`
- Modify: `src/app/Application.h`
- Modify: `src/app/Application.cpp`
- Create: `scripts/cycle_keyboard_layout.tmscript`
- Modify: `lang/en.ini`
- Modify: `lang/ru.ini`
- Modify: `tests/LocalizationTests.cpp`

**Interfaces:**
- Consumes: `ScriptManifest::Action::CycleKeyboardLayout` from Task 2 and the existing script execution gate.
- Produces: `FindNextInstalledLayout(HKL, const std::vector<HKL>&) -> HKL`, `Application::ExecuteBuiltinAction`, and `Application::CycleForegroundKeyboardLayout`.
- Keeps: `ExecuteScript` as the single reservation entry point for UI and hotkey execution.

- [ ] **Step 1: Add failing next-layout tests**

Extend `tests/OutputLayoutTests.cpp`:

```cpp
const HKL german = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000407));
Check(FindNextInstalledLayout(english, { english, russian, german }) == russian,
      "cycle selects the next installed layout");
Check(FindNextInstalledLayout(german, { english, russian, german }) == english,
      "cycle wraps to the first installed layout");
Check(FindNextInstalledLayout(english, { english }) == nullptr,
      "one installed layout is a no-op");
Check(FindNextInstalledLayout(german, { english, russian }) == nullptr,
      "missing current layout is a no-op");
```

- [ ] **Step 2: Run the output-layout target and verify it fails**

```powershell
rtk cmake --build build --config Release --target TextMagicOutputLayoutTests
```

Expected: compilation fails because `FindNextInstalledLayout` is undefined.

- [ ] **Step 3: Implement the pure cycle selector**

Add to `OutputLayout.h`:

```cpp
inline HKL FindNextInstalledLayout(HKL current, const std::vector<HKL>& layouts) {
    if (!current || layouts.size() < 2) {
        return nullptr;
    }
    const auto currentIt = std::find(layouts.begin(), layouts.end(), current);
    if (currentIt == layouts.end()) {
        return nullptr;
    }
    const size_t nextIndex = (static_cast<size_t>(currentIt - layouts.begin()) + 1)
        % layouts.size();
    return layouts[nextIndex];
}
```

Include `<algorithm>`. Run the focused output-layout test and confirm it passes.

- [ ] **Step 4: Add the manifest-only bundled script**

Create `scripts/cycle_keyboard_layout.tmscript` with exactly:

```text
name=Next Keyboard Layout
description=Switches to the next installed keyboard layout
hotkey=Shift
action=cycle_keyboard_layout
enabled=true
```

Do not add a `---` body or PowerShell command.

- [ ] **Step 5: Add localized action status**

Add English and Russian values for:

```ini
app.status.layout_cycle_success=Keyboard layout switched.
app.status.layout_cycle_unavailable=Keyboard layout could not be switched.
app.log.script.builtin_layout_cycle=[Script] Built-in action: cycle keyboard layout.
```

Extend `tests/LocalizationTests.cpp` to assert all three keys are non-empty in both embedded languages.

- [ ] **Step 6: Execute the built-in action before the text pipeline**

Declare in `Application.h`:

```cpp
void ExecuteBuiltinAction(ScriptManifest::Action action);
bool CycleForegroundKeyboardLayout();
```

At the start of `ExecuteScript`, after successfully reserving the execution gate but before reading input or creating a worker thread:

```cpp
if (script.manifest.action != ScriptManifest::Action::TransformText) {
    ExecuteBuiltinAction(script.manifest.action);
    m_scriptExecutionGate.Release(GetTickCount64());
    return;
}
```

Implement `CycleForegroundKeyboardLayout` with `GetForegroundWindow`, `GetWindowThreadProcessId`, `GetKeyboardLayout`, `GetKeyboardLayoutList`, `FindNextInstalledLayout`, and `PostMessageW(WM_INPUTLANGCHANGEREQUEST)`. Return `false` for an invalid window, missing thread/layout, fewer than two layouts, a missing current layout, or a failed post. `ExecuteBuiltinAction` sets the localized success/failure status and appends the built-in log line.

- [ ] **Step 7: Verify the built-in path and copied script**

Run:

```powershell
rtk cmake --build build --config Release --target TextMagic TextMagicOutputLayoutTests TextMagicLocalizationTests TextMagicScriptManifestTests
rtk ctest --test-dir build -C Release -R "TextMagic(OutputLayout|Localization|ScriptManifest)Tests" --output-on-failure
rtk powershell -NoProfile -Command "Test-Path -LiteralPath 'build\\bin\\Release\\scripts\\cycle_keyboard_layout.tmscript'"
```

Expected: all focused tests pass and the last command prints `True`.

- [ ] **Step 8: Commit Task 3**

```powershell
rtk git add src/app/OutputLayout.h tests/OutputLayoutTests.cpp src/app/Application.h src/app/Application.cpp scripts/cycle_keyboard_layout.tmscript lang/en.ini lang/ru.ini tests/LocalizationTests.cpp
rtk git commit -m "feat: add keyboard layout cycle action"
```

---

### Task 5: Integrate modifier gestures with hooks and the message loop

**Files:**
- Modify: `src/app/Application.cpp`
- Modify: `src/app/Application.h` only if the timer handler is kept as a member
- Modify: `tests/ScriptExecutionGateTests.cpp`

**Interfaces:**
- Consumes: `ModifierGestureResolver` from Task 3, `ScriptManifest::HotkeyKind` from Task 2, and `ExecuteScriptByHotkeyId`/`ScriptExecutionGate` from existing application code.
- Produces: registered resolver bindings, timer-driven deferred dispatch, generic cancellation from keyboard/mouse hooks, and removal of the old per-hotkey Shift-style pending-tap fields.

- [ ] **Step 1: Add failing hook-policy checks**

Extend `tests/ScriptExecutionGateTests.cpp` with small pure assertions used by integration:

```cpp
Expect(HotkeyDispatch::IsModifierVirtualKey(VK_LWIN),
       "Win participates in generic modifier gestures");
Expect(!HotkeyDispatch::IsModifierVirtualKey(VK_CAPITAL),
       "Caps Lock is not a modifier gesture key");
```

Move Caps Lock out of `IsModifierVirtualKey`; `HandleInputBufferKeyDown` already handles it explicitly and modifier gestures are limited to Ctrl/Alt/Shift/Win.

- [ ] **Step 2: Run the focused test and confirm the Caps Lock assertion fails**

```powershell
rtk cmake --build build --config Release --target TextMagicScriptExecutionGateTests
rtk ctest --test-dir build -C Release -R TextMagicScriptExecutionGateTests --output-on-failure
```

Expected: the new Caps Lock assertion fails.

- [ ] **Step 3: Register modifier gestures separately from conventional chords**

Include `ModifierGestureResolver.h` and add one global resolver beside `g_hookHotkeys`. Replace `HookHotkey::pendingTapTick` and `pendingTapVkCode` with resolver bindings.

During `RegisterHotkeys`:

```cpp
if (script.manifest.hotkeyKind == ScriptManifest::HotkeyKind::KeyChord) {
    AddHookHotkey(script.hotkeyId, currentHotkey.modifiers, currentHotkey.virtualKey);
} else {
    modifierBindings.push_back({
        script.hotkeyId,
        script.manifest.hotkeyKind,
        script.manifest.modifiers,
        script.manifest.virtualKey,
    });
}
```

After the loop, call `g_modifierGestureResolver.SetBindings(std::move(modifierBindings))`. Duplicate detection must compare normalized `hotkeyKind`, `modifiers`, and `virtualKey`; this makes `Ctrl+Shift` and `Shift+Ctrl` duplicates. `UnregisterHotkeys` resets resolver bindings and cancels its timer.

- [ ] **Step 4: Replace old duplicate-modifier dispatch with resolver decisions**

Delete `HOTKEY_DOUBLE_TAP_TIMEOUT_MS`, `IsDuplicateModifierHotkey`, `IsSameModifierTapKey`, `ResetPendingModifierTap`, and the duplicate-modifier branch inside `DispatchHookHotkeysOnKeyDown`. That function then handles conventional chords only.

Add a helper that reserves and posts a resolver-produced hotkey ID without consuming the modifier event:

```cpp
void DispatchResolvedModifierHotkey(int hotkeyId, ULONGLONG now) {
    if (hotkeyId == 0 || !g_scriptExecutionGate
        || !g_scriptExecutionGate->TryReserve(now)) {
        return;
    }
    if (!PostMessageW(g_hotkeyDispatchWindow, WM_HOTKEY,
                      static_cast<WPARAM>(hotkeyId), 0)) {
        g_scriptExecutionGate->Release(now);
    }
}
```

Use a distinct `MODIFIER_GESTURE_TIMER_ID`. Applying a resolver decision must kill the old timer, schedule `dueTick - now` with a minimum of 1 ms when `decision.pending`, and call `DispatchResolvedModifierHotkey` when `decision.hotkeyId != 0`.

- [ ] **Step 5: Feed all relevant hook events into the resolver**

In `InputKeyboardHookProc`, continue updating `g_hookKeyState` first. Then:

- On modifier key-down, call `OnKeyDown` and pass the event through.
- On modifier key-up, call `OnKeyUp` and pass the event through.
- On any non-modifier key-down, call `Cancel()` before conventional hotkey matching and input-buffer tracking.
- When foreground handling is blocked, cancel the resolver before passing the event through.

Use `reinterpret_cast<std::uintptr_t>(GetForegroundWindow())` as the pure context ID. In `InputMouseHookProc`, cancel on every physical button-down before clearing the input buffer.

- [ ] **Step 6: Resolve pending gestures from the main window timer**

Add to the main `Application::HandleMessage` switch:

```cpp
case WM_TIMER:
    if (wParam == MODIFIER_GESTURE_TIMER_ID) {
        KillTimer(m_hWnd, MODIFIER_GESTURE_TIMER_ID);
        const ULONGLONG now = GetTickCount64();
        ApplyModifierGestureDecision(
            g_modifierGestureResolver.OnTimeout(
                now,
                reinterpret_cast<std::uintptr_t>(GetForegroundWindow())),
            now);
        return 0;
    }
    break;
```

Cancel and kill this timer during script reload, hook uninstall, and application shutdown. Do not reuse the popup tracking timer ID.

- [ ] **Step 7: Run focused gesture and hook-policy tests**

```powershell
rtk cmake --build build --config Release --target TextMagicModifierGestureResolverTests TextMagicScriptExecutionGateTests TextMagicScriptManifestTests
rtk ctest --test-dir build -C Release -R "TextMagic(ModifierGestureResolver|ScriptExecutionGate|ScriptManifest)Tests" --output-on-failure
```

Expected: all focused tests pass, including Shift-vs-Shift+Shift and Ctrl-vs-Ctrl+Shift precedence.

- [ ] **Step 8: Run the complete clean verification**

```powershell
rtk cmake --build build --config Release --clean-first
rtk ctest --test-dir build -C Release --output-on-failure
rtk git diff --check
```

Expected: the application and every test target build, all tests pass, and `git diff --check` prints no errors.

- [ ] **Step 9: Manually smoke-test the actual hook**

Launch `build/bin/Release/TextMagic.exe` and verify:

1. A short Shift tap cycles to the next installed layout after the quiet window.
2. Shift held longer than 300 ms does nothing.
3. `Shift+A` types a capital letter without cycling.
4. `Shift+Shift` runs `layout_auto_qwerty.tmscript` and does not also cycle.
5. Change only `hotkey=Shift` to `hotkey=Ctrl`, reload scripts, and confirm Ctrl now owns the single-modifier action.
6. Change only the manifest to `hotkey=Ctrl+Shift`, reload, and confirm the modifier-only chord works.
7. Change only the manifest to `hotkey=Ctrl+Alt+L`, reload, and confirm the conventional chord dispatches immediately.
8. Restore the bundled script to `hotkey=Shift` before committing.

- [ ] **Step 10: Commit Task 4**

```powershell
rtk git add src/app/Application.cpp src/app/Application.h src/app/ScriptExecutionGate.h tests/ScriptExecutionGateTests.cpp
rtk git commit -m "feat: dispatch deferred modifier hotkeys"
```

---

## Completion Check

Before claiming completion:

```powershell
rtk git status --short --branch
rtk git log -4 --oneline
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
```

Expected tracked state: only the intended implementation commits. The existing untracked `scripts/sample_lowercase.tmscript` remains unmodified and uncommitted.
