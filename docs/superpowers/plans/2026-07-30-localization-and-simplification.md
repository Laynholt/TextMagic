# Localization and Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop materializing bundled translations at runtime, preserve user language overrides, and apply the approved non-UI code reductions.

**Architecture:** `lang/*.ini` remains the manually edited build input and is embedded into `EmbeddedLanguages.h`. Runtime localization starts with embedded maps and overlays user-provided `.ini` files only when an external directory already exists. The remaining work is behavior-preserving removal of duplicated helpers, unused flexibility, and hand-written platform functionality.

**Tech Stack:** C++17, Win32, CMake 3.16+, CTest, Windows CryptoAPI.

## Global Constraints

- Russian remains the default application language.
- External `lang/<code>.ini` values override embedded values with the same keys.
- Missing external keys retain embedded fallback values.
- The application must not create `lang`, `ru.ini`, `en.ini`, or other language files.
- `lang/*.ini` files are manually maintained; the build performs no translation between languages.
- Custom widgets, rendering, styled dialogs, and context-menu implementations are out of scope.
- Preserve all pre-existing working-tree changes. Do not stage or commit implementation files in this dirty tree unless the user separately authorizes committing those changes.
- Add no third-party dependency; `crypt32` is a Windows system library.

---

## File Map

- `src/app/Localization.cpp`: embedded language loading, external overlay loading, fallback lookup.
- `src/app/Localization.h`: public string-key localization API.
- `tests/LocalizationTests.cpp`: filesystem and overlay regression checks.
- `src/core/scripts/ScriptRunner.cpp`: process execution, output decoding, Base64 command encoding.
- `tests/ScriptRunnerTests.cpp`: PowerShell execution characterization check.
- `src/core/common/EncodingUtils.{h,cpp}`: UTF-8/wide conversion.
- `src/core/common/PowerShellUtils.{h,cpp}`: PowerShell string escaping only.
- `src/app/AppUiHelpers.{h,cpp}`: shared dialog-filter construction and UI helpers.
- `src/app/Application.{h,cpp}`: application wiring and removal of one-call delegates.
- `src/core/text/TextBridge.cpp`: active-control text operations.
- `CMakeLists.txt`: generated-language source, test targets, Windows libraries, and build cleanup.

---

### Task 1: Lock Down Localization Override Behavior

**Files:**
- Create: `tests/LocalizationTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/app/Localization.cpp:157-430`

**Interfaces:**
- Consumes: `Localization::Initialize`, `Localization::SetCurrentLanguageCode`, `Localization::GetTextByName`, `Localization::GetAvailableLanguageCodes`.
- Produces: runtime loading that reads but never creates the external language directory.

- [ ] **Step 1: Add the localization regression executable**

Create `tests/LocalizationTests.cpp`:

```cpp
#include "Localization.h"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
fs::path TestRoot() {
    return fs::temp_directory_path()
        / (L"TextMagicLocalizationTests-" + std::to_wstring(GetCurrentProcessId()));
}

void WriteUtf8(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(output.good());
}

void TestDoesNotMaterializeEmbeddedLanguages(const fs::path& root) {
    const fs::path missingDirectory = root / L"missing";
    Localization::Initialize(missingDirectory.wstring());
    assert(!fs::exists(missingDirectory));
}

void TestExternalFileOverridesEmbeddedKeys(const fs::path& root) {
    const fs::path languageDirectory = root / L"overrides";
    fs::create_directories(languageDirectory);
    WriteUtf8(
        languageDirectory / L"ru.ini",
        "status.ready=User ready\n"
        "meta.language_name=User Russian\n"
    );

    Localization::Initialize(languageDirectory.wstring());
    assert(std::wstring(Localization::GetTextByName(L"status.ready", L"ru")) == L"User ready");
    assert(!std::wstring(Localization::GetTextByName(L"button.more", L"ru")).empty());
}

void TestExternalLanguageIsAvailable(const fs::path& root) {
    const fs::path languageDirectory = root / L"extra-language";
    fs::create_directories(languageDirectory);
    WriteUtf8(
        languageDirectory / L"zz.ini",
        "status.ready=Zed ready\n"
        "meta.language_name=Zed\n"
    );

    Localization::Initialize(languageDirectory.wstring());
    const auto codes = Localization::GetAvailableLanguageCodes();
    assert(std::find(codes.begin(), codes.end(), L"zz") != codes.end());
    Localization::SetCurrentLanguageCode(L"zz");
    assert(std::wstring(Localization::GetTextByName(L"status.ready")) == L"Zed ready");
    assert(!std::wstring(Localization::GetTextByName(L"button.more")).empty());
}
}

int main() {
    const fs::path root = TestRoot();
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);

    TestDoesNotMaterializeEmbeddedLanguages(root);
    TestExternalFileOverridesEmbeddedKeys(root);
    TestExternalLanguageIsAvailable(root);

    fs::remove_all(root, cleanupError);
    return 0;
}
```

Add this target after the existing `TextMagicInputBufferTests` target:

```cmake
add_executable(TextMagicLocalizationTests
    src/app/Localization.cpp
    src/core/common/EncodingUtils.cpp
    tests/LocalizationTests.cpp
    ${TM_EMBEDDED_LANG_HEADER}
)

target_include_directories(TextMagicLocalizationTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

target_compile_features(TextMagicLocalizationTests PRIVATE cxx_std_17)

add_test(NAME TextMagicLocalizationTests COMMAND TextMagicLocalizationTests)
```

- [ ] **Step 2: Verify the regression test fails for file materialization**

Run:

```powershell
rtk cmake -S . -B build
rtk cmake --build build --config Debug --target TextMagicLocalizationTests
rtk ctest --test-dir build -C Debug -R TextMagicLocalizationTests --output-on-failure
```

Expected: `TextMagicLocalizationTests` fails in
`TestDoesNotMaterializeEmbeddedLanguages` because current initialization
creates the missing directory and writes bundled language files.

- [ ] **Step 3: Remove runtime extraction of embedded languages**

In `src/app/Localization.cpp`:

- remove `g_embeddedLanguageFiles`;
- remove `WriteUtf8BytesFile`;
- remove `EnsureEmbeddedLanguageFileExists`;
- stop recording embedded file pointers in `LoadEmbeddedLanguageTexts`;
- replace directory creation and extraction in `Initialize` with a read-only
  existence check:

```cpp
const fs::path languageDirectory(langDirectory);
std::error_code statusError;
if (!fs::exists(languageDirectory, statusError)
    || statusError
    || !fs::is_directory(languageDirectory, statusError)
    || statusError) {
    return;
}
```

Keep the existing external-file merge:

```cpp
std::unordered_map<std::wstring, std::wstring> merged =
    BuildFallbackTexts(languageCode);
for (const auto& pair : loadedTexts) {
    merged[pair.first] = pair.second;
}
g_allLanguageTexts[languageCode] = std::move(merged);
```

- [ ] **Step 4: Verify localization behavior is green**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicLocalizationTests
rtk ctest --test-dir build -C Debug -R TextMagicLocalizationTests --output-on-failure
```

Expected: one test executed, zero failures.

- [ ] **Step 5: Record the task checkpoint without staging user changes**

Run:

```powershell
rtk git diff --check
rtk git diff --stat
```

Expected: no whitespace errors. Leave changes unstaged because `CMakeLists.txt`
already contains user-owned modifications.

---

### Task 2: Remove the Parallel Enum Localization API

**Files:**
- Modify: `src/app/Localization.h:6-51`
- Modify: `src/app/Localization.cpp:19-120,503-512`
- Modify: `src/app/Application.cpp:181-183` and all `Localization::Key` call sites

**Interfaces:**
- Consumes: `Localization::GetTextByName(const std::wstring&)`.
- Produces: one localization lookup API based on `.ini` keys.

- [ ] **Step 1: Confirm the string-key API is covered**

Run:

```powershell
rtk ctest --test-dir build -C Debug -R TextMagicLocalizationTests --output-on-failure
```

Expected: the localization test passes before the refactor.

- [ ] **Step 2: Replace enum call sites with their `.ini` keys**

Delete the local `L(Localization::Key)` wrapper from `Application.cpp`.
Replace every `L(Localization::Key::<Name>)` call with `T(L"<ini-key>")`
using this complete mapping:

```text
MenuMoreLogs=menu.more.logs
MenuMoreAbout=menu.more.about
MenuCopy=menu.copy
MenuSaveAs=menu.save_as
MenuClearLogs=menu.clear_logs
MenuScriptsAdd=menu.scripts.add
MenuScriptsImportZip=menu.scripts.import_zip
MenuScriptsExportZip=menu.scripts.export_zip
MenuScriptsEnable=menu.scripts.enable
MenuScriptsDisable=menu.scripts.disable
MenuScriptsDelete=menu.scripts.delete
MenuTrayExit=menu.tray.exit
MenuLanguageTitle=menu.language.title
MenuLanguageRussian=menu.language.russian
MenuLanguageEnglish=menu.language.english
HintLabel=hint.label
ButtonReloadScripts=button.reload_scripts
ButtonOpenScriptsFolder=button.open_scripts_folder
ButtonMore=button.more
StatusReady=status.ready
TooltipReload=tooltip.reload
TooltipScriptList=tooltip.script_list
TooltipOpenScriptsFolder=tooltip.open_scripts_folder
TooltipMore=tooltip.more
StatusLanguageUpdated=status.language_updated
StatusNoScriptsFound=status.no_scripts_found
ScriptListHotkeyUnavailablePrefix=script_list.hotkey_unavailable_prefix
StatusLogsCleared=status.logs_cleared
AboutLoadedScriptsPrefix=about.loaded_scripts_prefix
AboutScriptsDirectoryPrefix=about.scripts_directory_prefix
AboutCheckUpdatesHint=about.check_updates_hint
LogIsEmpty=log.is_empty
InfoButtonClose=info.button.close
InfoButtonCheckUpdates=info.button.check_updates
```

- [ ] **Step 3: Delete the enum lookup layer**

From `Localization.h`, remove:

```cpp
enum class Key { /* all values */ };
const wchar_t* GetText(Key key);
```

From `Localization.cpp`, remove:

```cpp
struct Entry;
constexpr Entry kEntries[];
const Entry* FindEntryByKey(Key key);
const wchar_t* GetText(Key key);
```

Do not change `GetTextByName`, external overlay loading, or fallback lookup.

- [ ] **Step 4: Verify no enum API remains and rebuild**

Run:

```powershell
rtk rg "Localization::Key|GetText\\(Key" src
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug --output-on-failure
```

Expected: the search returns no matches; the build succeeds; all tests pass.

- [ ] **Step 5: Record the task checkpoint**

Run:

```powershell
rtk git diff --check
rtk git diff --stat
```

Expected: no whitespace errors. Leave the overlapping application changes
unstaged.

---

### Task 3: Simplify Script Execution and Encoding

**Files:**
- Create: `tests/ScriptRunnerTests.cpp`
- Modify: `src/core/scripts/ScriptRunner.cpp:19-161,399-435`
- Modify: `src/core/common/EncodingUtils.h:6`
- Modify: `src/core/common/EncodingUtils.cpp:6-66`
- Modify: `src/core/common/PowerShellUtils.h`
- Modify: `src/core/common/PowerShellUtils.cpp`
- Modify: `src/core/update/UpdateService.cpp:358-405`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ScriptRunner::ExecutePowerShellScript` and `EncodingUtils::Utf8ToWide`.
- Produces: identical PowerShell execution using Windows Base64 encoding and fewer conversion/file helpers.

- [ ] **Step 1: Add a PowerShell execution characterization test**

Create `tests/ScriptRunnerTests.cpp`:

```cpp
#include "ScriptRunner.h"

#include <cassert>
#include <string>

int main() {
    ScriptRunner runner;
    std::wstring output;
    std::wstring error;
    const std::wstring script =
        L"[Console]::InputEncoding=[Text.Encoding]::UTF8\n"
        L"[Console]::OutputEncoding=[Text.Encoding]::UTF8\n"
        L"$value=[Console]::In.ReadToEnd()\n"
        L"[Console]::Out.Write($value)\n";

    const bool ok = runner.ExecutePowerShellScript(
        script,
        L"Hello, Привет",
        &output,
        &error
    );
    assert(ok);
    assert(error.empty());
    assert(output == L"Hello, Привет");
    return 0;
}
```

Add the test target:

```cmake
add_executable(TextMagicScriptRunnerTests
    src/app/Localization.cpp
    src/core/common/EncodingUtils.cpp
    src/core/common/PowerShellUtils.cpp
    src/core/scripts/ScriptRunner.cpp
    tests/ScriptRunnerTests.cpp
    ${TM_EMBEDDED_LANG_HEADER}
)

target_include_directories(TextMagicScriptRunnerTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
)

target_compile_features(TextMagicScriptRunnerTests PRIVATE cxx_std_17)
target_link_libraries(TextMagicScriptRunnerTests PRIVATE crypt32)

add_test(NAME TextMagicScriptRunnerTests COMMAND TextMagicScriptRunnerTests)
```

- [ ] **Step 2: Establish the current behavior**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicScriptRunnerTests
rtk ctest --test-dir build -C Debug -R TextMagicScriptRunnerTests --output-on-failure
```

Expected: the characterization test passes before refactoring. If
`powershell.exe` cannot be launched, stop and report that environmental
prerequisite instead of weakening the test.

- [ ] **Step 3: Reuse existing conversion and standard file I/O**

In `BytesToWide`, retain the UTF-16LE detection branch and replace its manual
UTF-8/ACP conversion tail with:

```cpp
return EncodingUtils::Utf8ToWide(text);
```

Replace `ReadFileToString` with:

```cpp
std::string ReadFileToString(const std::wstring& filePath) {
    std::ifstream input(std::filesystem::path(filePath), std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}
```

Add the required standard headers:

```cpp
#include <filesystem>
#include <fstream>
#include <iterator>
```

- [ ] **Step 4: Replace the hand-written Base64 encoder**

Include `<wincrypt.h>` and replace `Base64Encode` with:

```cpp
std::wstring Base64Encode(const unsigned char* data, size_t size) {
    if (!data || size == 0 || size > MAXDWORD) {
        return {};
    }

    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    DWORD outputLength = 0;
    if (!CryptBinaryToStringW(
            data,
            static_cast<DWORD>(size),
            flags,
            nullptr,
            &outputLength)) {
        return {};
    }

    std::wstring encoded(outputLength, L'\0');
    if (!CryptBinaryToStringW(
            data,
            static_cast<DWORD>(size),
            flags,
            encoded.data(),
            &outputLength)) {
        return {};
    }
    if (!encoded.empty() && encoded.back() == L'\0') {
        encoded.pop_back();
    }
    return encoded;
}
```

Link `crypt32` to both the main executable and
`TextMagicScriptRunnerTests`.

- [ ] **Step 5: Remove unused PowerShell and encoding flexibility**

Delete `PowerShellUtils::GetExecutableName` from its header and source.
Replace its two call sites in `ScriptRunner.cpp` and `UpdateService.cpp` with:

```cpp
L"powershell.exe"
```

Change the encoding declaration to:

```cpp
std::string WideToUtf8(const std::wstring& text);
```

Remove the `allowAnsiFallback` parameter and its never-selected early-return
branch from `WideToUtf8`; keep the existing ANSI fallback conversion.

- [ ] **Step 6: Verify script execution after each refactor**

Run:

```powershell
rtk cmake --build build --config Debug --target TextMagicScriptRunnerTests
rtk ctest --test-dir build -C Debug -R TextMagicScriptRunnerTests --output-on-failure
rtk cmake --build build --config Debug --target TextMagic
```

Expected: the script-runner test passes and the main target builds.

- [ ] **Step 7: Record the task checkpoint**

Run:

```powershell
rtk git diff --check
rtk git diff --stat
```

Expected: no whitespace errors. Leave the changes unstaged.

---

### Task 4: Remove One-Call Application Delegates

**Files:**
- Modify: `src/app/AppUiHelpers.h`
- Modify: `src/app/AppUiHelpers.cpp:18-33,109-115`
- Modify: `src/app/Application.h:112-197`
- Modify: `src/app/Application.cpp:650-738,1245-1439,3084-3837`
- Modify: `src/core/text/TextBridge.cpp:13-15`

**Interfaces:**
- Consumes: `ClipboardUtils::ReadText`, `ClipboardUtils::WriteText`,
  `ImportScriptFileToDirectory`, and `UpdateService`.
- Produces: one shared `BuildDialogFilter` and direct use of existing helpers.

- [ ] **Step 1: Share the existing dialog-filter builder**

Move this type and declaration to `AppUiHelpers.h`:

```cpp
struct DialogFilterEntry {
    const wchar_t* labelKey;
    const wchar_t* pattern;
};

std::wstring BuildDialogFilter(
    std::initializer_list<DialogFilterEntry> entries
);
```

Add `<initializer_list>` to the header. Keep the implementation in
`AppUiHelpers.cpp`, remove its anonymous-namespace copy of
`DialogFilterEntry`, and remove the duplicate type and function from
`Application.cpp`.

- [ ] **Step 2: Call ClipboardUtils directly**

Remove these declarations and definitions:

```cpp
bool CopyTextToClipboard(HWND ownerWindow, const std::wstring& text);
bool ReadTextFromClipboard(HWND ownerWindow, std::wstring* text);
```

Add `#include "ClipboardUtils.h"` to `Application.cpp`. Replace call sites
with:

```cpp
ClipboardUtils::WriteText(ownerWindow, text)
ClipboardUtils::ReadText(ownerWindow, &text)
```

Use the same direct calls inside `AppUiHelpers.cpp`.

- [ ] **Step 3: Remove the import delegator**

Delete `Application::ImportScriptFile` from `Application.h` and
`Application.cpp`. In its sole caller, use:

```cpp
ImportScriptFileToDirectory(m_scriptsDirectory, sourcePath, &copiedPath)
```

- [ ] **Step 4: Store UpdateService by value**

Change the member to:

```cpp
UpdateService m_updateService;
```

Remove:

```cpp
m_updateService = std::make_unique<UpdateService>();
m_updateService.reset();
if (!m_updateService) {
    m_updateService = std::make_unique<UpdateService>();
}
```

Change value copies from `*m_updateService` to `m_updateService`.

- [ ] **Step 5: Delete the unused TextBridge helper**

Remove the unreferenced anonymous-namespace function:

```cpp
bool IsWordSeparator(wchar_t ch) {
    return iswspace(ch);
}
```

Do not alter any custom UI, popup, renderer, tooltip, or context-menu code.

- [ ] **Step 6: Build and run the full current test suite**

Run:

```powershell
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug --output-on-failure
```

Expected: the build succeeds and every registered test passes.

- [ ] **Step 7: Record the task checkpoint**

Run:

```powershell
rtk git diff --check
rtk git diff --stat
```

Expected: no whitespace errors. Leave overlapping user changes unstaged.

---

### Task 5: Finish the CMake Cleanup and Verify the Product

**Files:**
- Modify: `CMakeLists.txt:45-145`
- Verify: all modified source and test files

**Interfaces:**
- Consumes: `${TM_EMBEDDED_LANG_HEADER}` and all test targets added above.
- Produces: a Windows-only build with no runtime language-file copy.

- [ ] **Step 1: Remove the redundant header list**

Delete `set(HEADERS ...)`. Keep the generated header attached directly:

```cmake
add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${TM_EMBEDDED_LANG_HEADER}
)
```

- [ ] **Step 2: Remove redundant Windows guards**

Replace:

```cmake
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()
```

with:

```cmake
add_definitions(-DUNICODE -D_UNICODE)
```

Remove the `if(WIN32)` / `endif()` surrounding the existing Windows system
libraries. Keep all current libraries and add `crypt32`.

- [ ] **Step 3: Stop copying bundled languages beside the executable**

Remove only these post-build commands:

```cmake
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lang"
COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/lang"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lang"
```

Keep post-build creation and copying of the `scripts` directory.

- [ ] **Step 4: Run fresh configure, complete build, and complete tests**

Run:

```powershell
rtk cmake -S . -B build/codex-verify
rtk cmake --build build/codex-verify --config Debug
rtk ctest --test-dir build/codex-verify -C Debug --output-on-failure
```

Expected: configure and build exit with code `0`; CTest reports zero failed
tests.

- [ ] **Step 5: Verify the language-output contract**

Run:

```powershell
rtk proxy pwsh -NoProfile -Command 'Test-Path -LiteralPath build/codex-verify/bin/lang'
rtk proxy pwsh -NoProfile -Command 'Test-Path -LiteralPath build/codex-verify/bin/Debug/lang'
```

Expected: both commands print `False`. Do not launch the GUI as part of this
check; the localization regression test already proves initialization does
not create the directory.

- [ ] **Step 6: Review only the intended diff**

Run:

```powershell
rtk git diff --check
rtk git status --short
rtk git diff --stat
```

Expected: no whitespace errors; all pre-existing user changes remain present;
the implementation remains unstaged for user review.
