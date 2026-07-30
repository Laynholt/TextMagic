#pragma once

#include <string>
#include <vector>

namespace Localization {
void Initialize(const std::wstring& langDirectory);
void SetCurrentLanguageCode(const std::wstring& languageCode);
const std::wstring& GetCurrentLanguageCode();
const wchar_t* GetTextByName(const std::wstring& key);
const wchar_t* GetTextByName(const std::wstring& key, const std::wstring& languageCode);
std::vector<std::wstring> GetAvailableLanguageCodes();
std::wstring GetLanguageDisplayName(const std::wstring& languageCode);
} // namespace Localization
