#pragma once

#include <string>
#include <vector>

namespace Localization {
enum class Key {
    MenuMoreLogs,
    MenuMoreAbout,
    MenuCopy,
    MenuSaveAs,
    MenuClearLogs,
    MenuScriptsAdd,
    MenuScriptsImportZip,
    MenuScriptsExportZip,
    MenuScriptsEnable,
    MenuScriptsDisable,
    MenuScriptsDelete,
    MenuTrayExit,
    MenuLanguageTitle,
    MenuLanguageRussian,
    MenuLanguageEnglish,
    HintLabel,
    ButtonReloadScripts,
    ButtonOpenScriptsFolder,
    ButtonMore,
    StatusReady,
    TooltipReload,
    TooltipScriptList,
    TooltipOpenScriptsFolder,
    TooltipMore,
    StatusLanguageUpdated,
    StatusNoScriptsFound,
    ScriptListHotkeyUnavailablePrefix,
    StatusLogsCleared,
    AboutLoadedScriptsPrefix,
    AboutScriptsDirectoryPrefix,
    AboutCheckUpdatesHint,
    LogIsEmpty,
    InfoButtonClose,
    InfoButtonCheckUpdates
};

void Initialize(const std::wstring& langDirectory);
void SetCurrentLanguageCode(const std::wstring& languageCode);
const std::wstring& GetCurrentLanguageCode();
const wchar_t* GetTextByName(const std::wstring& key);
const wchar_t* GetTextByName(const std::wstring& key, const std::wstring& languageCode);
std::vector<std::wstring> GetAvailableLanguageCodes();
std::wstring GetLanguageDisplayName(const std::wstring& languageCode);
const wchar_t* GetText(Key key);
} // namespace Localization
