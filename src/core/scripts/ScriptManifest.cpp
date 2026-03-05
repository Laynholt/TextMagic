#include "ScriptManifest.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace {
std::wstring Trim(const std::wstring& text) {
    size_t begin = 0;
    while (begin < text.size() && std::iswspace(text[begin])) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && std::iswspace(text[end - 1])) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::wstring ToUpperAscii(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch >= L'a' && ch <= L'z') {
            return static_cast<wchar_t>(ch - (L'a' - L'A'));
        }
        return ch;
    });
    return value;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), required);
    if (!result.empty() && result.front() == 0xFEFF) {
        result.erase(result.begin());
    }
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return std::string();
    }

    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::vector<std::wstring> Split(const std::wstring& text, wchar_t delimiter) {
    std::vector<std::wstring> parts;
    std::wstring current;
    for (wchar_t ch : text) {
        if (ch == delimiter) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

std::wstring ReplaceAll(std::wstring source, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) {
        return source;
    }

    size_t pos = 0;
    while ((pos = source.find(from, pos)) != std::wstring::npos) {
        source.replace(pos, from.size(), to);
        pos += to.size();
    }
    return source;
}

bool ParseVirtualKey(const std::wstring& token, UINT* virtualKey) {
    if (!virtualKey) {
        return false;
    }

    if (token.size() == 1) {
        const wchar_t ch = token[0];
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) {
            *virtualKey = static_cast<UINT>(ch);
            return true;
        }
    }

    if (token.size() >= 2 && token[0] == L'F') {
        const int keyIndex = _wtoi(token.c_str() + 1);
        if (keyIndex >= 1 && keyIndex <= 24) {
            *virtualKey = static_cast<UINT>(VK_F1 + (keyIndex - 1));
            return true;
        }
    }

    struct NameToVk {
        const wchar_t* name;
        UINT vk;
    };

    static const NameToVk map[] = {
        { L"SPACE", VK_SPACE },
        { L"TAB", VK_TAB },
        { L"ENTER", VK_RETURN },
        { L"RETURN", VK_RETURN },
        { L"ESC", VK_ESCAPE },
        { L"ESCAPE", VK_ESCAPE },
        { L"UP", VK_UP },
        { L"DOWN", VK_DOWN },
        { L"LEFT", VK_LEFT },
        { L"RIGHT", VK_RIGHT },
        { L"HOME", VK_HOME },
        { L"END", VK_END },
        { L"PGUP", VK_PRIOR },
        { L"PGDN", VK_NEXT },
        { L"PAGEUP", VK_PRIOR },
        { L"PAGEDOWN", VK_NEXT },
        { L"INSERT", VK_INSERT },
        { L"DELETE", VK_DELETE },
        { L"BACKSPACE", VK_BACK }
    };

    for (const auto& pair : map) {
        if (token == pair.name) {
            *virtualKey = pair.vk;
            return true;
        }
    }

    return false;
}

bool ParseEnabledValue(const std::wstring& value, bool defaultValue) {
    const std::wstring normalized = ToUpperAscii(Trim(value));
    if (normalized.empty()) {
        return defaultValue;
    }
    if (normalized == L"1" || normalized == L"TRUE" || normalized == L"YES" || normalized == L"ON") {
        return true;
    }
    if (normalized == L"0" || normalized == L"FALSE" || normalized == L"NO" || normalized == L"OFF") {
        return false;
    }
    return defaultValue;
}

bool ReadUtf8TextFile(const fs::path& path, std::wstring* text, std::wstring* error) {
    if (text) {
        text->clear();
    }
    if (error) {
        error->clear();
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) {
            *error = L"Не удалось прочитать файл: " + path.wstring();
        }
        return false;
    }

    const std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text) {
        *text = Utf8ToWide(raw);
    }
    return true;
}

bool WriteUtf8TextFile(const fs::path& path, const std::wstring& text, std::wstring* error) {
    if (error) {
        error->clear();
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) {
            *error = L"Не удалось открыть файл для записи: " + path.wstring();
        }
        return false;
    }

    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    output.write(reinterpret_cast<const char*>(bom), sizeof(bom));

    const std::string raw = WideToUtf8(text);
    if (!raw.empty()) {
        output.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }
    if (!output.good()) {
        if (error) {
            *error = L"Ошибка записи файла: " + path.wstring();
        }
        return false;
    }
    return true;
}

bool IsHeaderDelimiter(const std::wstring& line) {
    return Trim(line) == L"---";
}
}

ScriptManifest::LoadResult ScriptManifest::LoadFromDirectory(const std::wstring& directoryPath) {
    LoadResult result;
    std::wostringstream warnings;

    std::error_code existsError;
    if (!fs::exists(fs::path(directoryPath), existsError)) {
        return result;
    }

    std::error_code iterateError;
    for (const auto& entry : fs::directory_iterator(fs::path(directoryPath), iterateError)) {
        if (iterateError) {
            break;
        }

        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path path = entry.path();
        if (path.extension() != L".tmscript") {
            continue;
        }

        std::wstring wide;
        std::wstring readError;
        if (!ReadUtf8TextFile(path, &wide, &readError)) {
            warnings << L"[Load] " << readError << L"\n";
            continue;
        }

        const std::vector<std::wstring> lines = Split(wide, L'\n');
        std::map<std::wstring, std::wstring> fields;
        std::wstring scriptBody;
        bool inScriptBody = false;
        for (const auto& rawLine : lines) {
            std::wstring line = rawLine;
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }

            if (inScriptBody) {
                if (!scriptBody.empty()) {
                    scriptBody += L"\n";
                }
                scriptBody += line;
                continue;
            }

            const std::wstring trimmed = Trim(line);
            if (IsHeaderDelimiter(trimmed)) {
                inScriptBody = true;
                continue;
            }
            if (trimmed.empty() || trimmed[0] == L'#' || trimmed[0] == L';') {
                continue;
            }

            const size_t separatorPos = line.find(L'=');
            if (separatorPos == std::wstring::npos) {
                continue;
            }

            std::wstring key = Trim(line.substr(0, separatorPos));
            std::wstring value = Trim(line.substr(separatorPos + 1));
            key = ToUpperAscii(key);
            fields[key] = value;
        }

        ScriptManifest::Entry manifest;
        manifest.manifestPath = path.wstring();
        manifest.name = fields[L"NAME"];
        manifest.description = fields[L"DESCRIPTION"];
        manifest.hotkeyText = fields[L"HOTKEY"];
        manifest.commandLine = fields[L"COMMAND"];
        manifest.scriptBody = scriptBody;
        manifest.enabled = ParseEnabledValue(fields[L"ENABLED"], true);

        const bool hasInlineScript = !Trim(manifest.scriptBody).empty();
        const bool hasCommandLine = !Trim(manifest.commandLine).empty();

        if (manifest.name.empty() || manifest.hotkeyText.empty() || (!hasInlineScript && !hasCommandLine)) {
            warnings << L"[Parse] Пропуск " << path.filename().wstring()
                     << L": поля name/hotkey и script-body (или command) обязательны.\n";
            continue;
        }

        std::wstring hotkeyError;
        if (!ScriptManifest::ParseHotkey(manifest.hotkeyText, &manifest.modifiers, &manifest.virtualKey, &hotkeyError)) {
            warnings << L"[Parse] Пропуск " << path.filename().wstring()
                     << L": " << hotkeyError << L"\n";
            continue;
        }

        const std::wstring scriptDir = path.parent_path().wstring();
        manifest.commandLine = ReplaceAll(manifest.commandLine, L"%SCRIPT_DIR%", scriptDir);

        result.entries.push_back(std::move(manifest));
    }

    std::sort(result.entries.begin(), result.entries.end(), [](const Entry& lhs, const Entry& rhs) {
        return _wcsicmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
    });

    result.warning = warnings.str();
    return result;
}

bool ScriptManifest::SetEnabledInFile(const std::wstring& manifestPath, bool enabled, std::wstring* error) {
    if (error) {
        error->clear();
    }

    const fs::path path(manifestPath);
    std::wstring text;
    if (!ReadUtf8TextFile(path, &text, error)) {
        return false;
    }

    const std::vector<std::wstring> lines = Split(text, L'\n');
    std::vector<std::wstring> headerLines;
    std::vector<std::wstring> bodyLines;
    bool hasDelimiter = false;
    bool inBody = false;

    for (std::wstring line : lines) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (!inBody && IsHeaderDelimiter(line)) {
            hasDelimiter = true;
            inBody = true;
            continue;
        }
        if (inBody) {
            bodyLines.push_back(std::move(line));
        } else {
            headerLines.push_back(std::move(line));
        }
    }

    bool replaced = false;
    for (auto& line : headerLines) {
        const std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L'#' || trimmed[0] == L';') {
            continue;
        }
        const size_t separatorPos = line.find(L'=');
        if (separatorPos == std::wstring::npos) {
            continue;
        }
        const std::wstring key = ToUpperAscii(Trim(line.substr(0, separatorPos)));
        if (key == L"ENABLED") {
            line = enabled ? L"enabled=true" : L"enabled=false";
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        headerLines.push_back(enabled ? L"enabled=true" : L"enabled=false");
    }

    std::wstring updated;
    for (const auto& line : headerLines) {
        updated += line;
        updated += L"\r\n";
    }
    if (hasDelimiter) {
        updated += L"---\r\n";
        for (size_t index = 0; index < bodyLines.size(); ++index) {
            updated += bodyLines[index];
            if (index + 1 < bodyLines.size()) {
                updated += L"\r\n";
            }
        }
    }

    return WriteUtf8TextFile(path, updated, error);
}

bool ScriptManifest::ParseHotkey(const std::wstring& hotkeyText, UINT* modifiers, UINT* virtualKey, std::wstring* error) {
    if (!modifiers || !virtualKey) {
        if (error) {
            *error = L"Внутренняя ошибка: null output.";
        }
        return false;
    }

    *modifiers = 0;
    *virtualKey = 0;

    const std::vector<std::wstring> parts = Split(hotkeyText, L'+');
    bool keyFound = false;

    for (const std::wstring& rawToken : parts) {
        const std::wstring token = ToUpperAscii(Trim(rawToken));
        if (token.empty()) {
            continue;
        }

        if (token == L"CTRL" || token == L"CONTROL") {
            *modifiers |= MOD_CONTROL;
            continue;
        }
        if (token == L"ALT") {
            *modifiers |= MOD_ALT;
            continue;
        }
        if (token == L"SHIFT") {
            *modifiers |= MOD_SHIFT;
            continue;
        }
        if (token == L"WIN" || token == L"WINDOWS") {
            *modifiers |= MOD_WIN;
            continue;
        }

        if (keyFound) {
            if (error) {
                *error = L"В hotkey должен быть только один основной ключ.";
            }
            return false;
        }

        if (!ParseVirtualKey(token, virtualKey)) {
            if (error) {
                *error = L"Неизвестный основной ключ: " + token;
            }
            return false;
        }
        keyFound = true;
    }

    if (!keyFound) {
        if (error) {
            *error = L"Не найден основной ключ hotkey.";
        }
        return false;
    }

    if (*modifiers == 0) {
        if (error) {
            *error = L"Hotkey должен содержать хотя бы один модификатор (Ctrl/Alt/Shift/Win).";
        }
        return false;
    }

    return true;
}
