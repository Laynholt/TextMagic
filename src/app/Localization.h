#pragma once

#include <string>

namespace Localization {
enum class Language {
    Russian = 0,
    English = 1
};

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
const wchar_t* GetText(Key key, Language language);
} // namespace Localization
