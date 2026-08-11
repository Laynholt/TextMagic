#include "Localization.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool g_allChecksPassed = true;

void Check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
        g_allChecksPassed = false;
    }
}

#define CHECK(expression) Check((expression), #expression, __FILE__, __LINE__)

fs::path TestRoot() {
    return fs::temp_directory_path()
        / (L"TextMagicLocalizationTests-" + std::to_wstring(GetCurrentProcessId()));
}

void WriteUtf8(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    CHECK(output.good());
}

void TestDoesNotMaterializeEmbeddedLanguages(const fs::path& root) {
    const fs::path missingDirectory = root / L"missing";
    Localization::Initialize(missingDirectory.wstring());
    CHECK(!fs::exists(missingDirectory));
}

void TestExternalFileOverridesEmbeddedKeys(const fs::path& root) {
    const fs::path languageDirectory = root / L"overrides";
    fs::create_directories(languageDirectory);
    const std::wstring embeddedButtonMore = Localization::GetTextByName(L"button.more", L"ru");
    WriteUtf8(
        languageDirectory / L"ru.ini",
        "status.ready=User ready\n"
        "meta.language_name=User Russian\n"
    );

    Localization::Initialize(languageDirectory.wstring());
    CHECK(std::wstring(Localization::GetTextByName(L"status.ready", L"ru")) == L"User ready");
    CHECK(std::wstring(Localization::GetTextByName(L"button.more", L"ru")) == embeddedButtonMore);
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
    CHECK(std::find(codes.begin(), codes.end(), L"zz") != codes.end());
    Localization::SetCurrentLanguageCode(L"zz");
    CHECK(std::wstring(Localization::GetTextByName(L"status.ready")) == L"Zed ready");
    CHECK(!std::wstring(Localization::GetTextByName(L"button.more")).empty());
}

void TestEmbeddedLanguagesContainApplicationBlacklistKeys() {
    CHECK(std::wstring(Localization::GetTextByName(L"menu.application_blacklist", L"en"))
        == L"Application blacklist...");
    CHECK(std::wstring(Localization::GetTextByName(L"application_blacklist.disable_fullscreen", L"en"))
        == L"Disable hotkeys in fullscreen applications");
    CHECK(std::wstring(Localization::GetTextByName(L"application_blacklist.add_selected", L"en"))
        == L"Add selected");
    CHECK(std::wstring(Localization::GetTextByName(L"menu.application_blacklist", L"ru"))
        == L"\u0427\u0451\u0440\u043D\u044B\u0439 \u0441\u043F\u0438\u0441\u043E\u043A "
           L"\u043F\u0440\u0438\u043B\u043E\u0436\u0435\u043D\u0438\u0439...");
    CHECK(std::wstring(Localization::GetTextByName(L"application_blacklist.disable_fullscreen", L"ru"))
        == L"\u041E\u0442\u043A\u043B\u044E\u0447\u0430\u0442\u044C "
           L"\u0433\u043E\u0440\u044F\u0447\u0438\u0435 "
           L"\u043A\u043B\u0430\u0432\u0438\u0448\u0438 \u0432 "
           L"\u043F\u043E\u043B\u043D\u043E\u044D\u043A\u0440\u0430\u043D\u043D\u044B\u0445 "
           L"\u043F\u0440\u0438\u043B\u043E\u0436\u0435\u043D\u0438\u044F\u0445");
}

void TestEmbeddedLanguagesContainScriptActionKeys() {
    CHECK(std::wstring(Localization::GetTextByName(
        L"manifest.error.unknown_action_prefix", L"en"))
        == L"Unsupported script action: ");
    CHECK(std::wstring(Localization::GetTextByName(
        L"manifest.error.unknown_action_prefix", L"ru"))
        == L"\u041D\u0435\u043F\u043E\u0434\u0434\u0435\u0440\u0436\u0438\u0432\u0430\u0435\u043C\u043E\u0435 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u0435 \u0441\u043A\u0440\u0438\u043F\u0442\u0430: ");
    CHECK(!std::wstring(Localization::GetTextByName(
        L"manifest.warning.required_fields", L"en")).empty());
    CHECK(!std::wstring(Localization::GetTextByName(
        L"manifest.warning.required_fields", L"ru")).empty());

    constexpr const wchar_t* layoutCycleKeys[] = {
        L"app.status.layout_cycle_success",
        L"app.status.layout_cycle_unavailable",
        L"app.log.script.builtin_layout_cycle",
    };
    for (const wchar_t* key : layoutCycleKeys) {
        CHECK(!std::wstring(Localization::GetTextByName(key, L"en")).empty());
        CHECK(!std::wstring(Localization::GetTextByName(key, L"ru")).empty());
    }
}

void TestEmbeddedLanguagesContainConciseMainHint() {
    const std::wstring english = Localization::GetTextByName(L"hint.label", L"en");
    const std::wstring russian = Localization::GetTextByName(L"hint.label", L"ru");

    CHECK(english ==
        L"Scripts work with selected text, the last typed word, or all typed text.\r\n"
        L"Double-click a script to apply it to text from the clipboard.");
    CHECK(russian ==
        L"\u0421\u043a\u0440\u0438\u043f\u0442\u044b \u0440\u0430\u0431\u043e\u0442\u0430\u044e\u0442 \u0441 \u0432\u044b\u0434\u0435\u043b\u0435\u043d\u043d\u044b\u043c \u0442\u0435\u043a\u0441\u0442\u043e\u043c, "
        L"\u043f\u043e\u0441\u043b\u0435\u0434\u043d\u0438\u043c \u0432\u0432\u0435\u0434\u0451\u043d\u043d\u044b\u043c \u0441\u043b\u043e\u0432\u043e\u043c \u0438\u043b\u0438 \u0432\u0441\u0435\u043c \u0432\u0432\u0435\u0434\u0451\u043d\u043d\u044b\u043c \u0442\u0435\u043a\u0441\u0442\u043e\u043c.\r\n"
        L"\u0414\u0432\u043e\u0439\u043d\u043e\u0439 \u0449\u0435\u043b\u0447\u043e\u043a \u043f\u0440\u0438\u043c\u0435\u043d\u044f\u0435\u0442 \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u0439 \u0441\u043a\u0440\u0438\u043f\u0442 \u043a \u0442\u0435\u043a\u0441\u0442\u0443 \u0438\u0437 \u0431\u0443\u0444\u0435\u0440\u0430 \u043e\u0431\u043c\u0435\u043d\u0430.");
    CHECK(english.find(L"Global text scripts") == std::wstring::npos);
    CHECK(russian.find(
        L"\u0413\u043b\u043e\u0431\u0430\u043b\u044c\u043d\u044b\u0435 \u0441\u043a\u0440\u0438\u043f\u0442\u044b")
        == std::wstring::npos);
}

void TestCurrentLanguageIsSafeDuringConcurrentSwitches(const fs::path& root) {
    const fs::path languageDirectory = root / L"concurrent-language";
    fs::create_directories(languageDirectory);
    const std::wstring alphaCode(64, L'a');
    const std::wstring bravoCode(64, L'b');
    WriteUtf8(languageDirectory / (alphaCode + L".ini"), "status.ready=Alpha ready\n");
    WriteUtf8(languageDirectory / (bravoCode + L".ini"), "status.ready=Bravo ready\n");
    Localization::Initialize(languageDirectory.wstring());

    Localization::SetCurrentLanguageCode(alphaCode);
    const std::wstring& stableLanguageCode = Localization::GetCurrentLanguageCode();
    const wchar_t* stableText = Localization::GetTextByName(L"status.ready");

    std::atomic<bool> start{false};
    std::atomic<bool> sawUnexpectedText{false};
    std::thread writer([&]() {
        while (!start.load(std::memory_order_acquire)) {
        }
        for (int iteration = 0; iteration < 50000; ++iteration) {
            Localization::SetCurrentLanguageCode(
                (iteration % 2 == 0) ? alphaCode : bravoCode
            );
        }
    });

    std::vector<std::thread> readers;
    for (int reader = 0; reader < 3; ++reader) {
        readers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
            }
            for (int iteration = 0; iteration < 50000; ++iteration) {
                const std::wstring text = Localization::GetTextByName(L"status.ready");
                if (text != L"Alpha ready" && text != L"Bravo ready") {
                    sawUnexpectedText.store(true, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    writer.join();
    for (std::thread& reader : readers) {
        reader.join();
    }

    Localization::SetCurrentLanguageCode(bravoCode);
    CHECK(!sawUnexpectedText.load(std::memory_order_relaxed));
    CHECK(stableLanguageCode == alphaCode);
    CHECK(std::wstring(stableText) == L"Alpha ready");
}
}

int main() {
    const fs::path root = TestRoot();
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);

    TestDoesNotMaterializeEmbeddedLanguages(root);
    TestExternalFileOverridesEmbeddedKeys(root);
    TestExternalLanguageIsAvailable(root);
    TestEmbeddedLanguagesContainApplicationBlacklistKeys();
    TestEmbeddedLanguagesContainScriptActionKeys();
    TestEmbeddedLanguagesContainConciseMainHint();
    TestCurrentLanguageIsSafeDuringConcurrentSwitches(root);

    fs::remove_all(root, cleanupError);
    return g_allChecksPassed ? 0 : 1;
}
