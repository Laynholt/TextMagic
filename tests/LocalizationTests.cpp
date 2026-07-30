#include "Localization.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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
}

int main() {
    const fs::path root = TestRoot();
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);

    TestDoesNotMaterializeEmbeddedLanguages(root);
    TestExternalFileOverridesEmbeddedKeys(root);
    TestExternalLanguageIsAvailable(root);

    fs::remove_all(root, cleanupError);
    return g_allChecksPassed ? 0 : 1;
}
