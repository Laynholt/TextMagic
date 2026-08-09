# Layout Auto QWERTY Case and Language Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve case during RU/EN QWERTY conversion and switch the original target application's keyboard layout to the converted text's language.

**Architecture:** The script manifest opts into automatic output-layout handling with `output_layout=auto`. A small pure header selects English, Russian, or no change from the applied result; `Application` performs the Windows layout request only after a successful in-place replacement.

**Tech Stack:** C++17, Win32, PowerShell `.tmscript`, CMake, CTest.

## Global Constraints

- Switch layout only for successful in-place execution, never clipboard mode.
- Scripts without `output_layout=auto` keep their current behavior.
- A tie between Latin and Cyrillic letters, punctuation-only output, or a missing target layout leaves the layout unchanged.
- Preserve `scripts/sample_lowercase.tmscript` and every unrelated user change.
- Add no dependency or general-purpose abstraction.

---

### Task 1: Manifest Opt-In and Case Regression Coverage

**Files:**
- Modify: `src/core/scripts/ScriptManifest.h`
- Modify: `src/core/scripts/ScriptManifest.cpp`
- Modify: `scripts/layout_auto_qwerty.tmscript`
- Modify: `tests/ScriptManifestTests.cpp`
- Modify: `tests/ScriptRunnerTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `ScriptManifest::Entry::autoOutputLayout` as a `bool`, defaulting to `false`.
- Produces: `output_layout=auto` in the bundled layout script manifest.

- [ ] **Step 1: Write failing manifest tests**

Extend the temporary manifests in `tests/ScriptManifestTests.cpp` and assert literal behavior:

```cpp
const auto autoEntry = FindByName(result.entries, L"Auto");
const auto defaultEntry = FindByName(result.entries, L"Default");
const auto invalidEntry = FindByName(result.entries, L"Invalid");
passed &= Check(autoEntry && autoEntry->autoOutputLayout,
    "output_layout=auto enables automatic output layout");
passed &= Check(defaultEntry && !defaultEntry->autoOutputLayout,
    "missing output_layout keeps automatic output layout disabled");
passed &= Check(invalidEntry && !invalidEntry->autoOutputLayout,
    "unsupported output_layout stays disabled");
```

- [ ] **Step 2: Run the manifest test and verify RED**

Run:

```powershell
rtk cmake --build build/verify --config Release --target TextMagicScriptManifestTests
rtk ctest --test-dir build/verify -C Release -R TextMagicScriptManifestTests --output-on-failure
```

Expected: compile failure because `autoOutputLayout` does not exist.

- [ ] **Step 3: Implement the smallest manifest parser change**

Add to `ScriptManifest::Entry`:

```cpp
bool autoOutputLayout = false;
```

Populate it in `LoadFromDirectory`:

```cpp
manifest.autoOutputLayout =
    ToUpperAscii(Trim(fields[L"OUTPUT_LAYOUT"])) == L"AUTO";
```

Add this header field to `scripts/layout_auto_qwerty.tmscript`:

```ini
output_layout=auto
```

- [ ] **Step 4: Add real script case assertions**

Make `TextMagicScriptRunnerTests` compile `ScriptManifest.cpp`, define `TEXTMAGIC_SOURCE_DIR`, load the bundled manifest, and execute its real `scriptBody`. Assert these literal pairs:

```cpp
CheckConversion(runner, layoutScript.scriptBody, L"ghbdtn", L"привет");
CheckConversion(runner, layoutScript.scriptBody, L"Ghbdtn", L"Привет");
CheckConversion(runner, layoutScript.scriptBody, L"GHBDTN", L"ПРИВЕТ");
CheckConversion(runner, layoutScript.scriptBody, L"привет", L"ghbdtn");
CheckConversion(runner, layoutScript.scriptBody, L"Привет", L"Ghbdtn");
CheckConversion(runner, layoutScript.scriptBody, L"ПРИВЕТ", L"GHBDTN");
```

The CMake source-root definition is:

```cmake
target_compile_definitions(TextMagicScriptRunnerTests PRIVATE
    TEXTMAGIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
```

- [ ] **Step 5: Run focused tests and verify GREEN**

Run:

```powershell
rtk cmake --build build/verify --config Release --target TextMagicScriptManifestTests TextMagicScriptRunnerTests
rtk ctest --test-dir build/verify -C Release -R "TextMagicScript(Manifest|Runner)Tests" --output-on-failure
```

Expected: both tests pass and the six case conversions match exactly.

- [ ] **Step 6: Commit the task**

```powershell
rtk git add -- CMakeLists.txt scripts/layout_auto_qwerty.tmscript src/core/scripts/ScriptManifest.h src/core/scripts/ScriptManifest.cpp tests/ScriptManifestTests.cpp tests/ScriptRunnerTests.cpp
rtk git commit -m "feat: preserve layout conversion case metadata"
```

### Task 2: Output Language Selection

**Files:**
- Create: `src/app/OutputLayout.h`
- Create: `tests/OutputLayoutTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `enum class OutputLayout { Unchanged, English, Russian }`.
- Produces: `DetectOutputLayout(const std::wstring&) -> OutputLayout`.
- Produces: `ChooseOutputLayoutForAppliedScript(bool enabled, bool clipboardMode, bool replacementSucceeded, const std::wstring&) -> OutputLayout`.
- Produces: `FindInstalledOutputLayout(OutputLayout, const std::vector<HKL>&) -> HKL`.

- [ ] **Step 1: Write the failing selector test**

Create `tests/OutputLayoutTests.cpp` with literal checks:

```cpp
Check(DetectOutputLayout(L"hello") == OutputLayout::English, "Latin output selects English");
Check(DetectOutputLayout(L"привет") == OutputLayout::Russian, "Cyrillic output selects Russian");
Check(DetectOutputLayout(L"123 !") == OutputLayout::Unchanged, "non-letters keep the layout");
Check(DetectOutputLayout(L"abcабв") == OutputLayout::Unchanged, "equal alphabets keep the layout");
Check(DetectOutputLayout(L"hello мир") == OutputLayout::English, "Latin majority selects English");
Check(DetectOutputLayout(L"hello привет") == OutputLayout::Russian, "Cyrillic majority selects Russian");
Check(ChooseOutputLayoutForAppliedScript(true, false, true, L"привет") == OutputLayout::Russian,
    "successful in-place opt-in selects the output language");
Check(ChooseOutputLayoutForAppliedScript(false, false, true, L"привет") == OutputLayout::Unchanged,
    "scripts must opt in");
Check(ChooseOutputLayoutForAppliedScript(true, true, true, L"привет") == OutputLayout::Unchanged,
    "clipboard mode never switches layout");
Check(ChooseOutputLayoutForAppliedScript(true, false, false, L"привет") == OutputLayout::Unchanged,
    "failed replacement never switches layout");
const HKL english = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000409));
const HKL russian = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000419));
Check(FindInstalledOutputLayout(OutputLayout::English, { russian, english }) == english,
    "English selects an installed English layout");
Check(FindInstalledOutputLayout(OutputLayout::Russian, { english, russian }) == russian,
    "Russian selects an installed Russian layout");
Check(FindInstalledOutputLayout(OutputLayout::English, { russian }) == nullptr,
    "a missing target language keeps the current layout");
```

Register `TextMagicOutputLayoutTests` in CMake.

- [ ] **Step 2: Build and verify RED**

Run:

```powershell
rtk cmake --build build/verify --config Release --target TextMagicOutputLayoutTests
```

Expected: compile failure because `OutputLayout.h` does not exist.

- [ ] **Step 3: Implement the pure selector**

Create `src/app/OutputLayout.h` as a header-only helper. Count ASCII Latin letters and Unicode Cyrillic code points `U+0400` through `U+04FF`; return the majority or `Unchanged` on a tie. Gate detection behind the three execution conditions:

```cpp
inline OutputLayout ChooseOutputLayoutForAppliedScript(
    bool enabled,
    bool clipboardMode,
    bool replacementSucceeded,
    const std::wstring& output
) {
    if (!enabled || clipboardMode || !replacementSucceeded) {
        return OutputLayout::Unchanged;
    }
    return DetectOutputLayout(output);
}
```

`FindInstalledOutputLayout` scans the supplied `HKL` values and returns the first whose primary language matches `LANG_ENGLISH` or `LANG_RUSSIAN`; it returns `nullptr` for `Unchanged` or when no matching layout exists.

- [ ] **Step 4: Run the focused test and verify GREEN**

```powershell
rtk cmake --build build/verify --config Release --target TextMagicOutputLayoutTests
rtk ctest --test-dir build/verify -C Release -R TextMagicOutputLayoutTests --output-on-failure
```

Expected: all selector assertions pass.

- [ ] **Step 5: Commit the task**

```powershell
rtk git add -- CMakeLists.txt src/app/OutputLayout.h tests/OutputLayoutTests.cpp
rtk git commit -m "feat: detect output keyboard layout"
```

### Task 3: Apply the Target Window Layout

**Files:**
- Modify: `src/app/Application.cpp`

**Interfaces:**
- Consumes: `ScriptManifest::Entry::autoOutputLayout`.
- Consumes: `ChooseOutputLayoutForAppliedScript(...)`.
- Consumes: `FindInstalledOutputLayout(...)`.
- Produces: a best-effort `WM_INPUTLANGCHANGEREQUEST` to the original target window.

- [ ] **Step 1: Carry the opt-in through execution**

Add `bool autoOutputLayout = false;` to `ScriptExecutionTaskResult`, capture `script.manifest.autoOutputLayout` before starting the worker, and store the original `inputTargetWindow` in the result for every non-clipboard execution.

- [ ] **Step 2: Add the minimal Win32 request helper**

In the anonymous namespace, enumerate installed layouts with `GetKeyboardLayoutList`, pass them to `FindInstalledOutputLayout`, and send:

```cpp
PostMessageW(
    targetWindow,
    WM_INPUTLANGCHANGEREQUEST,
    0,
    reinterpret_cast<LPARAM>(keyboardLayout)
);
```

Return without side effects when the target window is invalid or no matching layout is installed.

- [ ] **Step 3: Request layout only after successful replacement**

After `replaceOk` is confirmed, compute:

```cpp
const OutputLayout outputLayout = ChooseOutputLayoutForAppliedScript(
    result->autoOutputLayout,
    result->clipboardMode,
    replaceOk,
    result->outputText
);
RequestKeyboardLayout(result->inputTargetWindow, outputLayout);
```

`RequestKeyboardLayout` treats `OutputLayout::Unchanged` as a no-op.

- [ ] **Step 4: Build the application and rerun focused tests**

```powershell
rtk cmake --build build/verify --config Release --target TextMagic TextMagicOutputLayoutTests TextMagicScriptManifestTests TextMagicScriptRunnerTests
rtk ctest --test-dir build/verify -C Release -R "TextMagic(OutputLayout|ScriptManifest|ScriptRunner)Tests" --output-on-failure
```

Expected: application compiles and all focused tests pass.

- [ ] **Step 5: Commit the task**

```powershell
rtk git add -- src/app/Application.cpp
rtk git commit -m "feat: switch to converted text layout"
```

### Task 4: Full Verification

**Files:**
- Modify only if indexing refresh changes it: `.codebase-memory/artifact.json`
- Modify only if indexing refresh changes it: `.codebase-memory/graph.db.zst`

**Interfaces:**
- Consumes: the complete feature.
- Produces: a clean Release build, green CTest suite, and refreshed repository graph.

- [ ] **Step 1: Reconfigure and build cleanly**

```powershell
rtk cmake --fresh -S . -B build/verify
rtk cmake --build build/verify --config Release --clean-first
```

Expected: all application and test targets compile without warnings or errors.

- [ ] **Step 2: Run the complete suite**

```powershell
rtk ctest --test-dir build/verify -C Release --output-on-failure
```

Expected: every test passes.

- [ ] **Step 3: Refresh the codebase graph**

Run the repository indexer in fast persistence mode. If tracked graph files change, stage and commit only those files:

```powershell
rtk git add -- .codebase-memory/artifact.json .codebase-memory/graph.db.zst
rtk git commit -m "chore: refresh codebase memory metadata"
```

- [ ] **Step 4: Verify final scope**

```powershell
rtk git status --short --branch
rtk git diff HEAD~4..HEAD --check
```

Expected: only the pre-existing untracked `scripts/sample_lowercase.tmscript` may remain outside committed feature changes.
