#include "Localization.h"

#include "EmbeddedLanguages.h"
#include "EncodingUtils.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace Localization {
namespace {
namespace fs = std::filesystem;

bool g_isInitialized = false;
std::wstring g_currentLanguageCode = L"ru";
std::unordered_map<std::wstring, std::unordered_map<std::wstring, std::wstring>> g_embeddedLanguageTexts;
std::unordered_map<std::wstring, std::unordered_map<std::wstring, std::wstring>> g_allLanguageTexts;

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring Trim(const std::wstring& value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring UnescapeIniValue(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && i + 1 < value.size()) {
            ++i;
            switch (value[i]) {
            case L'n':
                result.push_back(L'\n');
                break;
            case L'r':
                result.push_back(L'\r');
                break;
            case L't':
                result.push_back(L'\t');
                break;
            case L'\\':
                result.push_back(L'\\');
                break;
            default:
                result.push_back(value[i]);
                break;
            }
            continue;
        }
        result.push_back(value[i]);
    }
    return result;
}

bool ReadUtf8TextFile(const fs::path& path, std::wstring* text) {
    if (!text) {
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        text->clear();
        return false;
    }

    const std::string data(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>()
    );

    std::wstring wide;
    if (data.size() >= 3
        && static_cast<unsigned char>(data[0]) == 0xEF
        && static_cast<unsigned char>(data[1]) == 0xBB
        && static_cast<unsigned char>(data[2]) == 0xBF) {
        wide = EncodingUtils::Utf8ToWide(data.substr(3), true, false);
    } else {
        wide = EncodingUtils::Utf8ToWide(data, true, false);
    }

    if (wide.empty() && !data.empty()) {
        text->clear();
        return false;
    }

    *text = std::move(wide);
    return true;
}

bool ReadUtf8Bytes(const unsigned char* data, size_t size, std::wstring* text) {
    if (!data || !text) {
        return false;
    }

    std::string bytes(reinterpret_cast<const char*>(data), size);
    if (bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xEF
        && static_cast<unsigned char>(bytes[1]) == 0xBB
        && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }

    const std::wstring wide = EncodingUtils::Utf8ToWide(bytes, true, false);
    if (wide.empty() && !bytes.empty()) {
        text->clear();
        return false;
    }

    *text = wide;
    return true;
}

void LoadLanguageMapFromContent(const std::wstring& content,
                                std::unordered_map<std::wstring, std::wstring>* targetTexts) {
    if (!targetTexts) {
        return;
    }

    targetTexts->clear();

    size_t start = 0;
    while (start <= content.size()) {
        size_t end = content.find(L'\n', start);
        std::wstring line = end == std::wstring::npos
            ? content.substr(start)
            : content.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }

        const std::wstring trimmedLine = Trim(line);
        if (!trimmedLine.empty() && trimmedLine[0] != L';' && trimmedLine[0] != L'#') {
            const size_t separator = line.find(L'=');
            if (separator != std::wstring::npos) {
                const std::wstring key = Trim(line.substr(0, separator));
                const std::wstring value = UnescapeIniValue(line.substr(separator + 1));
                (*targetTexts)[key] = value;
            }
        }

        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
}

void LoadLanguageFile(const fs::path& filePath,
                      std::unordered_map<std::wstring, std::wstring>* targetTexts) {
    std::wstring content;
    if (!ReadUtf8TextFile(filePath, &content)) {
        if (targetTexts) {
            targetTexts->clear();
        }
        return;
    }
    LoadLanguageMapFromContent(content, targetTexts);
}

void LoadEmbeddedLanguageTexts() {
    g_embeddedLanguageTexts.clear();
    g_allLanguageTexts.clear();

    for (size_t index = 0; index < EmbeddedLanguageFiles::kFileCount; ++index) {
        const EmbeddedLanguageFiles::File& file = EmbeddedLanguageFiles::kFiles[index];
        const std::wstring languageCode = ToLower(Trim(file.languageCode ? file.languageCode : L""));
        if (languageCode.empty()) {
            continue;
        }

        std::wstring content;
        if (!ReadUtf8Bytes(file.utf8Data, file.utf8Size, &content)) {
            continue;
        }

        std::unordered_map<std::wstring, std::wstring> texts;
        LoadLanguageMapFromContent(content, &texts);
        if (texts.empty()) {
            continue;
        }

        g_embeddedLanguageTexts[languageCode] = texts;
        g_allLanguageTexts[languageCode] = std::move(texts);
    }
}

std::unordered_map<std::wstring, std::wstring> BuildFallbackTexts(const std::wstring& languageCode) {
    const auto exactIt = g_embeddedLanguageTexts.find(languageCode);
    if (exactIt != g_embeddedLanguageTexts.end()) {
        return exactIt->second;
    }

    const auto englishIt = g_embeddedLanguageTexts.find(L"en");
    if (languageCode == L"en" && englishIt != g_embeddedLanguageTexts.end()) {
        return englishIt->second;
    }

    const auto russianIt = g_embeddedLanguageTexts.find(L"ru");
    if (russianIt != g_embeddedLanguageTexts.end()) {
        return russianIt->second;
    }

    if (englishIt != g_embeddedLanguageTexts.end()) {
        return englishIt->second;
    }

    if (!g_embeddedLanguageTexts.empty()) {
        return g_embeddedLanguageTexts.begin()->second;
    }

    return {};
}

const wchar_t* FindTextInMap(const std::unordered_map<std::wstring, std::wstring>& texts,
                             const std::wstring& key) {
    const auto textIt = texts.find(key);
    if (textIt == texts.end() || textIt->second.empty()) {
        return nullptr;
    }
    return textIt->second.c_str();
}

const wchar_t* FindTextInLanguage(const std::wstring& key, const std::wstring& languageCode) {
    const std::wstring normalizedKey = Trim(key);
    if (normalizedKey.empty()) {
        return L"";
    }

    const std::wstring normalizedLanguageCode = ToLower(Trim(languageCode));
    if (!normalizedLanguageCode.empty()) {
        const auto languageIt = g_allLanguageTexts.find(normalizedLanguageCode);
        if (languageIt != g_allLanguageTexts.end()) {
            const wchar_t* exactText = FindTextInMap(languageIt->second, normalizedKey);
            if (exactText) {
                return exactText;
            }
        }
    }

    if (normalizedLanguageCode != L"ru") {
        const auto russianIt = g_allLanguageTexts.find(L"ru");
        if (russianIt != g_allLanguageTexts.end()) {
            const wchar_t* russianText = FindTextInMap(russianIt->second, normalizedKey);
            if (russianText) {
                return russianText;
            }
        }
    }

    if (normalizedLanguageCode != L"en") {
        const auto englishIt = g_allLanguageTexts.find(L"en");
        if (englishIt != g_allLanguageTexts.end()) {
            const wchar_t* englishText = FindTextInMap(englishIt->second, normalizedKey);
            if (englishText) {
                return englishText;
            }
        }
    }

    for (const auto& pair : g_allLanguageTexts) {
        const wchar_t* text = FindTextInMap(pair.second, normalizedKey);
        if (text) {
            return text;
        }
    }

    return L"";
}

void EnsureInitialized() {
    if (g_isInitialized) {
        return;
    }

    LoadEmbeddedLanguageTexts();
    g_isInitialized = true;
}
} // namespace

void Initialize(const std::wstring& langDirectory) {
    LoadEmbeddedLanguageTexts();
    g_isInitialized = true;

    if (langDirectory.empty()) {
        return;
    }

    const fs::path languageDirectory(langDirectory);
    std::error_code statusError;
    if (!fs::exists(languageDirectory, statusError)
        || statusError
        || !fs::is_directory(languageDirectory, statusError)
        || statusError) {
        return;
    }

    std::error_code iterateError;
    for (const auto& entry : fs::directory_iterator(languageDirectory, iterateError)) {
        if (iterateError) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path filePath = entry.path();
        if (_wcsicmp(filePath.extension().wstring().c_str(), L".ini") != 0) {
            continue;
        }

        const std::wstring languageCode = ToLower(Trim(filePath.stem().wstring()));
        if (languageCode.empty()) {
            continue;
        }

        std::unordered_map<std::wstring, std::wstring> loadedTexts;
        LoadLanguageFile(filePath, &loadedTexts);
        if (loadedTexts.empty()) {
            continue;
        }

        std::unordered_map<std::wstring, std::wstring> merged = BuildFallbackTexts(languageCode);
        for (const auto& pair : loadedTexts) {
            merged[pair.first] = pair.second;
        }
        if (merged.find(L"meta.language_name") == merged.end()) {
            merged[L"meta.language_name"] = languageCode;
        }
        g_allLanguageTexts[languageCode] = std::move(merged);
    }
}

void SetCurrentLanguageCode(const std::wstring& languageCode) {
    std::wstring normalized = ToLower(Trim(languageCode));
    if (normalized.empty()) {
        normalized = L"ru";
    }
    g_currentLanguageCode = normalized;
}

const std::wstring& GetCurrentLanguageCode() {
    return g_currentLanguageCode;
}

const wchar_t* GetTextByName(const std::wstring& key) {
    EnsureInitialized();
    return FindTextInLanguage(key, g_currentLanguageCode);
}

const wchar_t* GetTextByName(const std::wstring& key, const std::wstring& languageCode) {
    EnsureInitialized();
    return FindTextInLanguage(key, languageCode);
}

std::vector<std::wstring> GetAvailableLanguageCodes() {
    EnsureInitialized();

    std::vector<std::wstring> codes;
    codes.reserve(g_allLanguageTexts.size());
    for (const auto& pair : g_allLanguageTexts) {
        if (!pair.first.empty()) {
            codes.push_back(pair.first);
        }
    }

    std::sort(codes.begin(), codes.end(), [](const std::wstring& left, const std::wstring& right) {
        if (left == right) {
            return false;
        }
        if (left == L"ru") {
            return true;
        }
        if (right == L"ru") {
            return false;
        }
        if (left == L"en") {
            return true;
        }
        if (right == L"en") {
            return false;
        }
        return left < right;
    });
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    return codes;
}

std::wstring GetLanguageDisplayName(const std::wstring& languageCode) {
    EnsureInitialized();

    const std::wstring normalizedCode = ToLower(Trim(languageCode));
    if (normalizedCode.empty()) {
        return L"";
    }

    const wchar_t* localizedName = FindTextInLanguage(L"meta.language_name", normalizedCode);
    if (localizedName && localizedName[0] != L'\0') {
        return localizedName;
    }

    return normalizedCode;
}

} // namespace Localization
