#include "Localization.h"

#include <windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace Localization {
namespace {
namespace fs = std::filesystem;

struct Entry {
    Key key;
    const wchar_t* iniName;
    const wchar_t* ru;
    const wchar_t* en;
};

constexpr size_t kKeyCount = static_cast<size_t>(Key::InfoButtonCheckUpdates) + 1;

constexpr Entry kEntries[] = {
    { Key::MenuMoreLogs, L"menu.more.logs", L"Логи выполнения", L"Execution Logs" },
    { Key::MenuMoreAbout, L"menu.more.about", L"О программе", L"About" },
    { Key::MenuCopy, L"menu.copy", L"Копировать", L"Copy" },
    { Key::MenuSaveAs, L"menu.save_as", L"Сохранить как...", L"Save As..." },
    { Key::MenuClearLogs, L"menu.clear_logs", L"Очистить логи", L"Clear Logs" },
    { Key::MenuScriptsAdd, L"menu.scripts.add", L"Добавить скрипт", L"Add Script" },
    { Key::MenuScriptsImportZip, L"menu.scripts.import_zip", L"Импорт из ZIP", L"Import from ZIP" },
    { Key::MenuScriptsExportZip, L"menu.scripts.export_zip", L"Экспорт всех скриптов в ZIP", L"Export All Scripts to ZIP" },
    { Key::MenuScriptsEnable, L"menu.scripts.enable", L"Включить выбранные", L"Enable Selected" },
    { Key::MenuScriptsDisable, L"menu.scripts.disable", L"Отключить выбранные", L"Disable Selected" },
    { Key::MenuScriptsDelete, L"menu.scripts.delete", L"Удалить выбранные", L"Delete Selected" },
    { Key::MenuTrayExit, L"menu.tray.exit", L"Закрыть", L"Exit" },
    { Key::MenuLanguageTitle, L"menu.language.title", L"Язык", L"Language" },
    { Key::HintLabel, L"hint.label",
      L"Глобальные скрипты для текста. Если есть выделение, обработка применяется к выделению.\r\n"
      L"Если выделения нет, берется весь текст из активного поля ввода. ПКМ по списку: управление/импорт/экспорт.",
      L"Global text scripts. If there is a selection, processing is applied to the selection.\r\n"
      L"If there is no selection, text is taken from the active input field. Right-click list for management/import/export." },
    { Key::ButtonReloadScripts, L"button.reload_scripts", L"Перезагрузить скрипты", L"Reload Scripts" },
    { Key::ButtonOpenScriptsFolder, L"button.open_scripts_folder", L"Открыть папку scripts", L"Open scripts Folder" },
    { Key::ButtonMore, L"button.more", L"Дополнительно", L"More" },
    { Key::StatusReady, L"status.ready", L"Готово.", L"Ready." },
    { Key::TooltipReload, L"tooltip.reload", L"Заново читает *.tmscript из папки scripts", L"Reloads *.tmscript from scripts folder" },
    { Key::TooltipScriptList, L"tooltip.script_list", L"Двойной клик: запуск. Правый клик: управление скриптами.", L"Double click: run. Right click: script actions." },
    { Key::TooltipOpenScriptsFolder, L"tooltip.open_scripts_folder", L"Открывает каталог scripts рядом с .exe", L"Opens scripts folder near .exe" },
    { Key::TooltipMore, L"tooltip.more", L"О программе, языках, обновлениях и логах", L"About, language, updates and logs" },
    { Key::StatusLanguageUpdated, L"status.language_updated", L"Язык приложения обновлен.", L"Application language updated." },
    { Key::StatusNoScriptsFound, L"status.no_scripts_found", L"Скрипты не найдены. Добавьте *.tmscript в папку scripts.", L"No scripts found. Add *.tmscript files to scripts folder." },
    { Key::ScriptListHotkeyUnavailablePrefix, L"script_list.hotkey_unavailable_prefix", L" (hotkey off: ", L" (hotkey unavailable: " },
    { Key::StatusLogsCleared, L"status.logs_cleared", L"Логи очищены.", L"Logs cleared." },
    { Key::AboutLoadedScriptsPrefix, L"about.loaded_scripts_prefix", L"Загружено скриптов: ", L"Loaded scripts: " },
    { Key::AboutScriptsDirectoryPrefix, L"about.scripts_directory_prefix", L"Каталог scripts:\r\n", L"Scripts directory:\r\n" },
    { Key::AboutCheckUpdatesHint, L"about.check_updates_hint", L"Проверьте обновления кнопкой ниже.", L"Use the button below to check for updates." },
    { Key::LogIsEmpty, L"log.is_empty", L"Лог пуст.", L"Log is empty." },
    { Key::InfoButtonClose, L"info.button.close", L"Закрыть", L"Close" },
    { Key::InfoButtonCheckUpdates, L"info.button.check_updates", L"Проверить обновления", L"Check Updates" }
};

std::array<std::wstring, kKeyCount> g_ruTexts;
std::array<std::wstring, kKeyCount> g_enTexts;
bool g_isInitialized = false;

size_t ToIndex(Key key) {
    return static_cast<size_t>(key);
}

const Entry* FindEntryByIniName(const std::wstring& iniName) {
    for (const Entry& entry : kEntries) {
        if (_wcsicmp(entry.iniName, iniName.c_str()) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

void LoadBuiltInTexts() {
    for (const Entry& entry : kEntries) {
        const size_t index = ToIndex(entry.key);
        if (index < kKeyCount) {
            g_ruTexts[index] = entry.ru;
            g_enTexts[index] = entry.en;
        }
    }
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

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }

    const int requiredSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0
    );
    if (requiredSize <= 0) {
        return std::wstring();
    }

    std::wstring wide(static_cast<size_t>(requiredSize), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        wide.data(),
        requiredSize
    );
    if (converted <= 0) {
        return std::wstring();
    }
    return wide;
}

bool ReadUtf8TextFile(const fs::path& path, std::wstring* text) {
    if (!text) {
        return false;
    }
    text->clear();

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    std::string data(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>()
    );
    if (data.size() >= 3
        && static_cast<unsigned char>(data[0]) == 0xEF
        && static_cast<unsigned char>(data[1]) == 0xBB
        && static_cast<unsigned char>(data[2]) == 0xBF) {
        data.erase(0, 3);
    }

    const std::wstring wide = Utf8ToWide(data);
    if (wide.empty() && !data.empty()) {
        return false;
    }
    *text = wide;
    return true;
}

void LoadLanguageOverrides(const fs::path& filePath, std::array<std::wstring, kKeyCount>* targetTexts) {
    if (!targetTexts) {
        return;
    }

    std::wstring content;
    if (!ReadUtf8TextFile(filePath, &content)) {
        return;
    }

    size_t start = 0;
    while (start <= content.size()) {
        size_t end = content.find(L'\n', start);
        std::wstring line = end == std::wstring::npos
            ? content.substr(start)
            : content.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        line = Trim(line);
        if (!line.empty() && line[0] != L';' && line[0] != L'#') {
            const size_t separator = line.find(L'=');
            if (separator != std::wstring::npos) {
                const std::wstring key = Trim(line.substr(0, separator));
                const std::wstring value = UnescapeIniValue(Trim(line.substr(separator + 1)));
                const Entry* entry = FindEntryByIniName(key);
                if (entry) {
                    const size_t index = ToIndex(entry->key);
                    if (index < kKeyCount) {
                        (*targetTexts)[index] = value;
                    }
                }
            }
        }

        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
}

void EnsureInitialized() {
    if (g_isInitialized) {
        return;
    }
    LoadBuiltInTexts();
    g_isInitialized = true;
}
} // namespace

void Initialize(const std::wstring& langDirectory) {
    LoadBuiltInTexts();
    g_isInitialized = true;

    if (langDirectory.empty()) {
        return;
    }

    const fs::path languageDirectory(langDirectory);
    std::error_code statusError;
    if (!fs::exists(languageDirectory, statusError) || statusError) {
        return;
    }
    if (!fs::is_directory(languageDirectory, statusError) || statusError) {
        return;
    }

    LoadLanguageOverrides(languageDirectory / L"ru.ini", &g_ruTexts);
    LoadLanguageOverrides(languageDirectory / L"en.ini", &g_enTexts);
}

const wchar_t* GetText(Key key, Language language) {
    EnsureInitialized();

    const size_t index = ToIndex(key);
    if (index >= kKeyCount) {
        return L"";
    }

    const std::wstring& result = language == Language::English ? g_enTexts[index] : g_ruTexts[index];
    return result.c_str();
}
} // namespace Localization
