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

void TestEmbeddedLanguagesContainHotkeyExclusionKeys() {
    CHECK(std::wstring(Localization::GetTextByName(L"menu.hotkey_exclusions", L"en"))
        == L"Hotkey exclusions...");
    CHECK(std::wstring(Localization::GetTextByName(L"hotkey_exclusions.disable_fullscreen", L"en"))
        == L"Disable hotkeys in fullscreen applications");
    CHECK(std::wstring(Localization::GetTextByName(L"menu.hotkey_exclusions", L"ru"))
        == L"\u0418\u0441\u043A\u043B\u044E\u0447\u0435\u043D\u0438\u044F "
           L"\u0433\u043E\u0440\u044F\u0447\u0438\u0445 "
           L"\u043A\u043B\u0430\u0432\u0438\u0448...");
    CHECK(std::wstring(Localization::GetTextByName(L"hotkey_exclusions.disable_fullscreen", L"ru"))
        == L"\u041E\u0442\u043A\u043B\u044E\u0447\u0430\u0442\u044C "
           L"\u0433\u043E\u0440\u044F\u0447\u0438\u0435 "
           L"\u043A\u043B\u0430\u0432\u0438\u0448\u0438 \u0432 "
           L"\u043F\u043E\u043B\u043D\u043E\u044D\u043A\u0440\u0430\u043D\u043D\u044B\u0445 "
           L"\u043F\u0440\u0438\u043B\u043E\u0436\u0435\u043D\u0438\u044F\u0445");
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
    TestEmbeddedLanguagesContainHotkeyExclusionKeys();
    TestCurrentLanguageIsSafeDuringConcurrentSwitches(root);

    fs::remove_all(root, cleanupError);
    return g_allChecksPassed ? 0 : 1;
}
