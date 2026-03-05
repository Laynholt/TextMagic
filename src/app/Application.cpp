#include "Application.h"

#include "Localization.h"
#include "ToolTip.h"
#include "UiRenderer.h"
#include "resource.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;

namespace {
const wchar_t* WINDOW_CLASS_NAME = L"TextMagicWinApiClass";
const wchar_t* INFO_WINDOW_CLASS_NAME = L"TextMagicInfoWindowClass";
const wchar_t* MESSAGE_WINDOW_CLASS_NAME = L"TextMagicMessageWindowClass";

enum ControlId {
    ID_SCRIPTS_LIST = 1001,
    ID_RELOAD_BUTTON = 1002,
    ID_OPEN_FOLDER_BUTTON = 1003,
    ID_MORE_BUTTON = 1004,
    ID_TITLE_LABEL = 1005,
    ID_HINT_LABEL = 1006,
    ID_STATUS_LABEL = 1007
};

enum MenuId {
    ID_MENU_MORE_ABOUT = 2001,
    ID_MENU_MORE_LOGS = 2002,
    ID_MENU_CONTEXT_COPY = 2003,
    ID_MENU_MORE_SEPARATOR = 2004,
    ID_MENU_CONTEXT_SAVEAS = 2005,
    ID_MENU_SCRIPTS_ENABLE = 2006,
    ID_MENU_SCRIPTS_DISABLE = 2007,
    ID_MENU_SCRIPTS_ADD = 2008,
    ID_MENU_SCRIPTS_DELETE = 2009,
    ID_MENU_SCRIPTS_IMPORT_ZIP = 2010,
    ID_MENU_SCRIPTS_EXPORT_ZIP = 2011,
    ID_MENU_CONTEXT_CLEAR_LOGS = 2012,
    ID_MENU_LANGUAGE_RU = 2013,
    ID_MENU_LANGUAGE_EN = 2014,
    ID_MENU_TRAY_EXIT = 2015,
    ID_MENU_LANGUAGE_LABEL = 2016
};

enum InfoControlId {
    ID_INFO_TEXT = 2101,
    ID_INFO_CLOSE = 2102,
    ID_INFO_ACTION = 2103
};

enum MessageControlId {
    ID_MESSAGE_TEXT = 2201,
    ID_MESSAGE_PRIMARY = 2202,
    ID_MESSAGE_SECONDARY = 2203
};

struct InfoWindowState {
    Application* owner = nullptr;
    int kind = 0;
    HWND titleLabel = nullptr;
    HWND textControl = nullptr;
    HWND closeButton = nullptr;
    HWND actionButton = nullptr;
    HWND contextMenuTarget = nullptr;
    std::wstring title;
    std::wstring text;
    bool usesListBox = false;
    HBRUSH editBrush = nullptr;
};

struct MessageWindowState {
    Application* owner = nullptr;
    HWND titleLabel = nullptr;
    HWND textControl = nullptr;
    HWND primaryButton = nullptr;
    HWND secondaryButton = nullptr;
    HWND contextMenuTarget = nullptr;
    std::wstring title;
    std::wstring text;
    std::wstring primaryButtonText;
    std::wstring secondaryButtonText;
    bool hasSecondaryButton = false;
    bool useMonoFont = false;
    int result = IDCANCEL;
    int* resultOut = nullptr;
    HBRUSH editBrush = nullptr;
};

constexpr int HOTKEY_BASE = 5000;
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT TRAY_ICON_ID = 1;
constexpr int LOGS_MIN_WIDTH = 640;
constexpr int LOGS_MIN_HEIGHT = 420;
constexpr int INFO_MIN_WIDTH = 500;
constexpr int INFO_MIN_HEIGHT = 300;

Localization::Language g_currentLanguage = Localization::Language::Russian;

const wchar_t* L(Localization::Key key) {
    return Localization::GetText(key, g_currentLanguage);
}

bool IsStyledMenuItem(UINT itemId);

HFONT GetStyledMenuFont() {
    static HFONT menuFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    return menuFont ? menuFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

HFONT GetTrayMenuFont() {
    static HFONT trayMenuFont = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    return trayMenuFont ? trayMenuFont : GetStyledMenuFont();
}

HFONT GetMenuFontForItem(UINT itemId) {
    return itemId == ID_MENU_TRAY_EXIT ? GetTrayMenuFont() : GetStyledMenuFont();
}

UINT ResolveStyledMenuItemId(UINT itemId, ULONG_PTR itemData) {
    if (IsStyledMenuItem(itemId)) {
        return itemId;
    }
    const UINT dataItemId = static_cast<UINT>(itemData);
    return IsStyledMenuItem(dataItemId) ? dataItemId : itemId;
}

const wchar_t* GetMenuItemText(UINT itemId) {
    switch (itemId) {
    case ID_MENU_MORE_LOGS:
        return L(Localization::Key::MenuMoreLogs);
    case ID_MENU_MORE_ABOUT:
        return L(Localization::Key::MenuMoreAbout);
    case ID_MENU_CONTEXT_COPY:
        return L(Localization::Key::MenuCopy);
    case ID_MENU_CONTEXT_SAVEAS:
        return L(Localization::Key::MenuSaveAs);
    case ID_MENU_CONTEXT_CLEAR_LOGS:
        return L(Localization::Key::MenuClearLogs);
    case ID_MENU_SCRIPTS_ADD:
        return L(Localization::Key::MenuScriptsAdd);
    case ID_MENU_SCRIPTS_IMPORT_ZIP:
        return L(Localization::Key::MenuScriptsImportZip);
    case ID_MENU_SCRIPTS_EXPORT_ZIP:
        return L(Localization::Key::MenuScriptsExportZip);
    case ID_MENU_SCRIPTS_ENABLE:
        return L(Localization::Key::MenuScriptsEnable);
    case ID_MENU_SCRIPTS_DISABLE:
        return L(Localization::Key::MenuScriptsDisable);
    case ID_MENU_SCRIPTS_DELETE:
        return L(Localization::Key::MenuScriptsDelete);
    case ID_MENU_LANGUAGE_RU:
        return L"Русский";
    case ID_MENU_LANGUAGE_EN:
        return L"English";
    case ID_MENU_TRAY_EXIT:
        return L(Localization::Key::MenuTrayExit);
    case ID_MENU_LANGUAGE_LABEL:
        return L(Localization::Key::MenuLanguageTitle);
    case ID_MENU_MORE_SEPARATOR:
        return L"";
    default:
        return L"";
    }
}

bool IsStyledMenuItem(UINT itemId) {
    return itemId == ID_MENU_MORE_LOGS || itemId == ID_MENU_MORE_ABOUT
        || itemId == ID_MENU_CONTEXT_COPY || itemId == ID_MENU_CONTEXT_SAVEAS
        || itemId == ID_MENU_CONTEXT_CLEAR_LOGS
        || itemId == ID_MENU_SCRIPTS_ADD || itemId == ID_MENU_SCRIPTS_IMPORT_ZIP
        || itemId == ID_MENU_SCRIPTS_EXPORT_ZIP || itemId == ID_MENU_SCRIPTS_ENABLE
        || itemId == ID_MENU_SCRIPTS_DISABLE || itemId == ID_MENU_SCRIPTS_DELETE
        || itemId == ID_MENU_LANGUAGE_RU || itemId == ID_MENU_LANGUAGE_EN
        || itemId == ID_MENU_LANGUAGE_LABEL
        || itemId == ID_MENU_TRAY_EXIT
        || itemId == ID_MENU_MORE_SEPARATOR;
}

const wchar_t* GetInfoWindowTitleByKind(int kind) {
    if (kind == 1) {
        return GetMenuItemText(ID_MENU_MORE_ABOUT);
    }
    if (kind == 2) {
        return GetMenuItemText(ID_MENU_MORE_LOGS);
    }
    return L"";
}

void MeasureStyledMenuItem(MEASUREITEMSTRUCT* mis) {
    if (!mis) {
        return;
    }

    const UINT itemId = ResolveStyledMenuItemId(mis->itemID, mis->itemData);
    if (itemId == ID_MENU_MORE_SEPARATOR) {
        mis->itemHeight = 10;
        mis->itemWidth = 170;
        return;
    }

    const wchar_t* text = GetMenuItemText(itemId);
    RECT textRect = { 0, 0, 0, 0 };
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        HFONT menuFont = GetMenuFontForItem(itemId);
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, menuFont));
        DrawTextW(hdc, text, -1, &textRect, DT_CALCRECT | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        ReleaseDC(nullptr, hdc);
    }
    const UINT minWidth = itemId == ID_MENU_TRAY_EXIT ? 130 : 170;
    const UINT extraWidth = itemId == ID_MENU_LANGUAGE_LABEL ? 48 : 34;
    mis->itemHeight = itemId == ID_MENU_TRAY_EXIT ? 30 : 34;
    mis->itemWidth = std::max<UINT>(minWidth, static_cast<UINT>((textRect.right - textRect.left) + extraWidth));
}

UINT GetResolvedMenuItemIdByPosition(HMENU menuHandle, int position) {
    if (!menuHandle || position < 0) {
        return 0;
    }

    MENUITEMINFOW menuItemInfo = {};
    menuItemInfo.cbSize = sizeof(menuItemInfo);
    menuItemInfo.fMask = MIIM_ID | MIIM_DATA;
    if (!GetMenuItemInfoW(menuHandle, static_cast<UINT>(position), TRUE, &menuItemInfo)) {
        return 0;
    }
    return ResolveStyledMenuItemId(menuItemInfo.wID, static_cast<ULONG_PTR>(menuItemInfo.dwItemData));
}

void DrawStyledMenuItem(const DRAWITEMSTRUCT* dis) {
    if (!dis) {
        return;
    }

    const UINT itemId = ResolveStyledMenuItemId(dis->itemID, dis->itemData);
    const bool isSeparator = itemId == ID_MENU_MORE_SEPARATOR;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool checked = (dis->itemState & ODS_CHECKED) != 0;

    COLORREF bgColor = selected ? RGB(66, 66, 66) : RGB(45, 45, 45);
    if (disabled) {
        bgColor = RGB(38, 38, 38);
    }

    HBRUSH bgBrush = CreateSolidBrush(RGB(45, 45, 45));
    FillRect(dis->hDC, &dis->rcItem, bgBrush);
    DeleteObject(bgBrush);

    if (isSeparator) {
        HPEN separatorPen = CreatePen(PS_SOLID, 1, RGB(78, 78, 78));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, separatorPen));
        const int y = (dis->rcItem.top + dis->rcItem.bottom) / 2;
        MoveToEx(dis->hDC, dis->rcItem.left + 12, y, nullptr);
        LineTo(dis->hDC, dis->rcItem.right - 12, y);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(separatorPen);
    } else {
        if (selected) {
            HBRUSH selectedBrush = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, selectedBrush);
            DeleteObject(selectedBrush);
        }

        RECT textRect = dis->rcItem;
        textRect.left += checked ? 28 : 14;
        textRect.right -= itemId == ID_MENU_LANGUAGE_LABEL ? 28 : 10;
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, disabled ? RGB(120, 120, 120) : RGB(235, 235, 235));

        if (checked) {
            SetTextColor(dis->hDC, disabled ? RGB(120, 120, 120) : RGB(190, 220, 255));
            RECT checkRect = dis->rcItem;
            checkRect.left += 10;
            checkRect.right = checkRect.left + 12;
            DrawTextW(dis->hDC, L"•", -1, &checkRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dis->hDC, disabled ? RGB(120, 120, 120) : RGB(235, 235, 235));
        }

        HFONT menuFont = GetMenuFontForItem(itemId);
        HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, menuFont));
        DrawTextW(dis->hDC, GetMenuItemText(itemId), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (itemId == ID_MENU_LANGUAGE_LABEL) {
            RECT arrowRect = dis->rcItem;
            arrowRect.right -= 12;
            arrowRect.left = arrowRect.right - 10;
            DrawTextW(dis->hDC, L">", -1, &arrowRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dis->hDC, oldFont);
    }

    HMENU menuHandle = reinterpret_cast<HMENU>(dis->hwndItem);
    const int itemCount = GetMenuItemCount(menuHandle);
    const UINT firstItemId = itemCount > 0 ? GetResolvedMenuItemIdByPosition(menuHandle, 0) : 0;
    const UINT lastItemId = itemCount > 0 ? GetResolvedMenuItemIdByPosition(menuHandle, itemCount - 1) : 0;
    const int borderWidth = 1;
    const COLORREF borderColor = RGB(62, 62, 62);
    HPEN borderPen = CreatePen(PS_SOLID, borderWidth, borderColor);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, borderPen));
    MoveToEx(dis->hDC, dis->rcItem.left, dis->rcItem.top, nullptr);
    LineTo(dis->hDC, dis->rcItem.left, dis->rcItem.bottom);
    MoveToEx(dis->hDC, dis->rcItem.right - 1, dis->rcItem.top, nullptr);
    LineTo(dis->hDC, dis->rcItem.right - 1, dis->rcItem.bottom);
    if (itemId == firstItemId) {
        MoveToEx(dis->hDC, dis->rcItem.left, dis->rcItem.top, nullptr);
        LineTo(dis->hDC, dis->rcItem.right, dis->rcItem.top);
    }
    if (itemId == lastItemId) {
        MoveToEx(dis->hDC, dis->rcItem.left, dis->rcItem.bottom - 1, nullptr);
        LineTo(dis->hDC, dis->rcItem.right, dis->rcItem.bottom - 1);
    }
    SelectObject(dis->hDC, oldPen);
    DeleteObject(borderPen);
}

void ShowStyledContextMenu(HWND ownerWindow, POINT screenPoint, bool includeSaveAs, bool includeClearLogs = false) {
    HMENU contextMenu = CreatePopupMenu();
    if (!contextMenu) {
        return;
    }

    static HBRUSH s_menuBrush = CreateSolidBrush(RGB(45, 45, 45));

    AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_CONTEXT_COPY, GetMenuItemText(ID_MENU_CONTEXT_COPY));
    if (includeSaveAs) {
        AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_CONTEXT_SAVEAS, GetMenuItemText(ID_MENU_CONTEXT_SAVEAS));
    }
    if (includeClearLogs) {
        if (includeSaveAs) {
            AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_MORE_SEPARATOR, GetMenuItemText(ID_MENU_MORE_SEPARATOR));
        }
        AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_CONTEXT_CLEAR_LOGS, GetMenuItemText(ID_MENU_CONTEXT_CLEAR_LOGS));
    }

    MENUINFO menuInfo = {};
    menuInfo.cbSize = sizeof(MENUINFO);
    menuInfo.fMask = MIM_BACKGROUND;
    menuInfo.hbrBack = s_menuBrush;
    SetMenuInfo(contextMenu, &menuInfo);

    SetForegroundWindow(ownerWindow);
    const UINT command = TrackPopupMenu(
        contextMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        screenPoint.x,
        screenPoint.y,
        0,
        ownerWindow,
        nullptr
    );
    if (command != 0) {
        SendMessageW(ownerWindow, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
    DestroyMenu(contextMenu);
}

bool CopyTextToClipboard(HWND ownerWindow, const std::wstring& text) {
    if (!OpenClipboard(ownerWindow)) {
        return false;
    }

    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memoryHandle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memoryHandle) {
        CloseClipboard();
        return false;
    }

    void* memory = GlobalLock(memoryHandle);
    if (!memory) {
        GlobalFree(memoryHandle);
        CloseClipboard();
        return false;
    }

    CopyMemory(memory, text.c_str(), bytes);
    GlobalUnlock(memoryHandle);

    if (!SetClipboardData(CF_UNICODETEXT, memoryHandle)) {
        GlobalFree(memoryHandle);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

void CopyEditSelectionOrAll(HWND editControl) {
    if (!editControl || !IsWindow(editControl)) {
        return;
    }

    SetFocus(editControl);
    const LRESULT selection = SendMessageW(editControl, EM_GETSEL, 0, 0);
    const int start = static_cast<int>(LOWORD(selection));
    const int end = static_cast<int>(HIWORD(selection));
    const bool noSelection = start == end;

    if (noSelection) {
        SendMessageW(editControl, EM_SETSEL, 0, -1);
    }
    SendMessageW(editControl, WM_COPY, 0, 0);
    if (noSelection) {
        SendMessageW(editControl, EM_SETSEL, start, start);
    }
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }

    const int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (requiredSize <= 0) {
        return std::string();
    }

    std::string result(static_cast<size_t>(requiredSize), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        requiredSize,
        nullptr,
        nullptr
    );
    return result;
}

bool SaveUtf8TextFile(const std::wstring& filePath, const std::wstring& text, std::wstring* error) {
    HANDLE fileHandle = CreateFileW(
        filePath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (fileHandle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = L"Не удалось открыть файл: " + std::to_wstring(GetLastError());
        }
        return false;
    }

    const std::string utf8 = WideToUtf8(text);
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    if (!WriteFile(fileHandle, bom, sizeof(bom), &written, nullptr)) {
        CloseHandle(fileHandle);
        if (error) {
            *error = L"Не удалось записать BOM: " + std::to_wstring(GetLastError());
        }
        return false;
    }

    if (!utf8.empty()) {
        written = 0;
        if (!WriteFile(fileHandle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
            CloseHandle(fileHandle);
            if (error) {
                *error = L"Не удалось записать файл: " + std::to_wstring(GetLastError());
            }
            return false;
        }
    }

    CloseHandle(fileHandle);
    return true;
}

bool SaveTextWithDialog(HWND ownerWindow, const std::wstring& text, std::wstring* savedPath, std::wstring* error) {
    if (savedPath) {
        savedPath->clear();
    }
    if (error) {
        error->clear();
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t defaultName[128] = {};
    swprintf_s(defaultName, L"TextMagic-logs-%04u%02u%02u-%02u%02u%02u.txt",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t filePath[MAX_PATH] = {};
    wcscpy_s(filePath, defaultName);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFilter = L"Текстовые файлы (*.txt)\0*.txt\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) {
        const DWORD dialogError = CommDlgExtendedError();
        if (dialogError != 0 && error) {
            *error = L"Ошибка диалога сохранения: " + std::to_wstring(dialogError);
        }
        return false;
    }

    if (!SaveUtf8TextFile(filePath, text, error)) {
        return false;
    }

    if (savedPath) {
        *savedPath = filePath;
    }
    return true;
}

void FillListBoxWithText(HWND listBox, const std::wstring& text) {
    if (!listBox || !IsWindow(listBox)) {
        return;
    }

    SendMessageW(listBox, WM_SETREDRAW, FALSE, 0);
    SendMessageW(listBox, LB_RESETCONTENT, 0, 0);

    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L"\r\n", start);
        std::wstring line = (end == std::wstring::npos) ? text.substr(start) : text.substr(start, end - start);
        if (!line.empty()) {
            SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 2;
    }

    const LRESULT count = SendMessageW(listBox, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        SendMessageW(listBox, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
    }
    SendMessageW(listBox, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(listBox, nullptr, TRUE);
}

POINT ResolveContextMenuPoint(HWND control, LPARAM lParam) {
    POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (point.x == -1 && point.y == -1 && control && IsWindow(control)) {
        RECT rect = {};
        GetWindowRect(control, &rect);
        point.x = rect.left + 10;
        point.y = rect.top + 10;
    }
    return point;
}

std::wstring GetSelectedListBoxText(HWND listBox) {
    const LRESULT selectedIndex = SendMessageW(listBox, LB_GETCURSEL, 0, 0);
    if (selectedIndex == LB_ERR) {
        return std::wstring();
    }
    const LRESULT textLen = SendMessageW(listBox, LB_GETTEXTLEN, static_cast<WPARAM>(selectedIndex), 0);
    if (textLen <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(textLen) + 1, L'\0');
    SendMessageW(listBox, LB_GETTEXT, static_cast<WPARAM>(selectedIndex), reinterpret_cast<LPARAM>(result.data()));
    result.resize(static_cast<size_t>(textLen));
    return result;
}

std::wstring ReadTextFromClipboard(HWND ownerWindow) {
    std::wstring result;
    if (!OpenClipboard(ownerWindow)) {
        return result;
    }

    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return result;
    }

    const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(data));
    if (ptr) {
        result = ptr;
        GlobalUnlock(data);
    }

    CloseClipboard();
    return result;
}

bool IsTmscriptFilePath(const fs::path& path) {
    const std::wstring extension = path.extension().wstring();
    return _wcsicmp(extension.c_str(), L".tmscript") == 0;
}

std::wstring EscapePowerShellSingleQuoted(const std::wstring& text) {
    std::wstring escaped = text;
    size_t pos = 0;
    while ((pos = escaped.find(L"'", pos)) != std::wstring::npos) {
        escaped.replace(pos, 1, L"''");
        pos += 2;
    }
    return escaped;
}

LRESULT CALLBACK CopyOnlyContextSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_CONTEXTMENU) {
        HWND ownerWindow = reinterpret_cast<HWND>(refData);
        if (ownerWindow && IsWindow(ownerWindow)) {
            SendMessageW(ownerWindow, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hWnd), lParam);
            return 0;
        }
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}
}

Application::Application() = default;

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_initializationError.clear();

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        m_initializationError = L"Не удалось инициализировать GDI+";
        return false;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(comResult)) {
        m_comInitialized = true;
    }

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    m_hBackgroundBrush = CreateSolidBrush(RGB(26, 26, 26));
    m_hCardBrush = CreateSolidBrush(RGB(45, 45, 45));
    m_hListBrush = CreateSolidBrush(RGB(37, 37, 37));

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WindowProc;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = m_hBackgroundBrush;
    wcex.lpszClassName = WINDOW_CLASS_NAME;
    wcex.hIconSm = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));

    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        m_initializationError = L"Не удалось зарегистрировать класс главного окна";
        return false;
    }

    if (!RegisterInfoWindowClass()) {
        m_initializationError = L"Не удалось зарегистрировать класс информационных окон";
        return false;
    }
    if (!RegisterMessageWindowClass()) {
        m_initializationError = L"Не удалось зарегистрировать класс styled message";
        return false;
    }

    const int windowWidth = 940;
    const int windowHeight = 620;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

    m_hWnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        x,
        y,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        m_hInstance,
        this
    );

    if (!m_hWnd) {
        m_initializationError = L"Не удалось создать главное окно";
        return false;
    }
    SetWindowTextW(m_hWnd, WINDOW_TITLE);
    DragAcceptFiles(m_hWnd, TRUE);
    InitializeTrayIcon();

    Localization::Initialize(GetExecutableDirectory() + L"\\lang");
    CreateControls();
    ApplyLocalization();
    m_updateService = std::make_unique<UpdateService>();

    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    OnResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);

    m_scriptsDirectory = GetExecutableDirectory() + L"\\scripts";
    std::error_code createDirError;
    fs::create_directories(fs::path(m_scriptsDirectory), createDirError);

    AppendLog(L"[App] Запуск TextMagic " + std::wstring(APP_VERSION) + L".");
    AppendLog(L"[App] Каталог scripts: " + m_scriptsDirectory);
    ReloadScripts(true);
    return true;
}

int Application::Run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (m_toolTip) {
            m_toolTip->RelayEvent(msg);
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void Application::Shutdown() {
    UnregisterHotkeys();
    RemoveTrayIcon();
    if (m_hWnd && IsWindow(m_hWnd)) {
        DragAcceptFiles(m_hWnd, FALSE);
    }

    if (m_hAboutWindow && IsWindow(m_hAboutWindow)) {
        DestroyWindow(m_hAboutWindow);
        m_hAboutWindow = nullptr;
    }
    if (m_hLogsWindow && IsWindow(m_hLogsWindow)) {
        DestroyWindow(m_hLogsWindow);
        m_hLogsWindow = nullptr;
    }

    m_toolTip.reset();
    m_updateService.reset();

    if (m_hMoreMenu) {
        DestroyMenu(m_hMoreMenu);
        m_hMoreMenu = nullptr;
        m_hLanguageMenu = nullptr;
    }

    if (m_hTitleFont) {
        DeleteObject(m_hTitleFont);
        m_hTitleFont = nullptr;
    }
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
    if (m_hMonoFont) {
        DeleteObject(m_hMonoFont);
        m_hMonoFont = nullptr;
    }

    if (m_hListBrush) {
        DeleteObject(m_hListBrush);
        m_hListBrush = nullptr;
    }
    if (m_hCardBrush) {
        DeleteObject(m_hCardBrush);
        m_hCardBrush = nullptr;
    }
    if (m_hBackgroundBrush) {
        DeleteObject(m_hBackgroundBrush);
        m_hBackgroundBrush = nullptr;
    }

    if (m_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }

    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}

LRESULT CALLBACK Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* app = reinterpret_cast<Application*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        if (app) {
            app->m_hWnd = hWnd;
        }
        return TRUE;
    }

    if (app) {
        return app->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        if (lParam == 0) {
            OnMenuCommand(LOWORD(wParam));
        } else {
            OnCommand(LOWORD(wParam), HIWORD(wParam));
        }
        return 0;

    case WM_MENUSELECT:
        if (m_moreMenuTracking && m_hMoreMenu && m_hLanguageMenu) {
            HMENU selectedMenu = reinterpret_cast<HMENU>(lParam);
            const DWORD now = GetTickCount();

            if (m_languageMenuPopupOpen && selectedMenu == m_hLanguageMenu) {
                RECT languageRect = {};
                const int itemCount = GetMenuItemCount(m_hMoreMenu);
                int languageIndex = -1;
                for (int i = 0; i < itemCount; ++i) {
                    if (GetMenuItemID(m_hMoreMenu, i) == ID_MENU_LANGUAGE_LABEL) {
                        languageIndex = i;
                        break;
                    }
                }
                if (languageIndex >= 0 && GetMenuItemRect(nullptr, m_hMoreMenu, static_cast<UINT>(languageIndex), &languageRect)) {
                    POINT cursor = {};
                    GetCursorPos(&cursor);
                    if (cursor.x < languageRect.right - 6 && now - m_languageMenuLastOpenTick > 80) {
                        m_languageMenuLastOpenTick = now;
                        PostMessageW(m_hWnd, WM_KEYDOWN, VK_LEFT, 0);
                    }
                }
            }

            if (m_languageMenuPopupOpen) {
                break;
            }

            if (HIWORD(wParam) == 0xFFFF && lParam == 0) {
                break;
            }

            const UINT selectedId = LOWORD(wParam);
            const UINT flags = HIWORD(wParam);
            if (selectedMenu == m_hMoreMenu
                && selectedId == ID_MENU_LANGUAGE_LABEL
                && (flags & (MF_GRAYED | MF_DISABLED)) == 0
                && now - m_languageMenuLastOpenTick > 120) {
                RECT itemRect = {};
                POINT openPoint = {};
                const int itemCount = GetMenuItemCount(m_hMoreMenu);
                int languageIndex = -1;
                for (int i = 0; i < itemCount; ++i) {
                    if (GetMenuItemID(m_hMoreMenu, i) == ID_MENU_LANGUAGE_LABEL) {
                        languageIndex = i;
                        break;
                    }
                }
                if (languageIndex >= 0 && GetMenuItemRect(nullptr, m_hMoreMenu, static_cast<UINT>(languageIndex), &itemRect)) {
                    openPoint.x = itemRect.right - 2;
                    openPoint.y = itemRect.top - 2;
                } else {
                    GetCursorPos(&openPoint);
                }
                ShowLanguageMenuPopup(openPoint, true);
                break;
            }
        }
        break;

    case WM_TRAYICON:
        {
            const UINT trayEvent = LOWORD(static_cast<DWORD_PTR>(lParam));
            switch (trayEvent) {
            case WM_LBUTTONUP:
            case NIN_SELECT:
            case NIN_KEYSELECT:
                RestoreFromTray();
                return 0;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                {
                    POINT cursor = {};
                    if (trayEvent == WM_CONTEXTMENU) {
                        cursor.x = GET_X_LPARAM(wParam);
                        cursor.y = GET_Y_LPARAM(wParam);
                        if (cursor.x == 0 && cursor.y == 0) {
                            GetCursorPos(&cursor);
                        }
                    } else {
                        GetCursorPos(&cursor);
                    }
                    ShowTrayContextMenu(cursor);
                }
                return 0;
            default:
                break;
            }
        }
        break;

    case WM_HOTKEY:
        ExecuteScriptByHotkeyId(static_cast<int>(wParam));
        return 0;

    case WM_DROPFILES:
        {
            HDROP dropHandle = reinterpret_cast<HDROP>(wParam);
            const UINT fileCount = DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> droppedPaths;
            droppedPaths.reserve(fileCount);
            for (UINT index = 0; index < fileCount; ++index) {
                wchar_t filePath[MAX_PATH] = {};
                const UINT length = DragQueryFileW(dropHandle, index, filePath, MAX_PATH);
                if (length > 0) {
                    droppedPaths.emplace_back(filePath);
                }
            }
            DragFinish(dropHandle);
            ImportScriptFiles(droppedPaths);
        }
        return 0;

    case WM_CONTEXTMENU:
        {
            HWND sourceControl = reinterpret_cast<HWND>(wParam);
            if (sourceControl == m_hScriptList) {
                const POINT point = ResolveContextMenuPoint(sourceControl, lParam);
                ShowScriptListContextMenu(point);
                return 0;
            }
        }
        break;

    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = MIN_WINDOW_WIDTH;
            info->ptMinTrackSize.y = MIN_WINDOW_HEIGHT;
        }
        return 0;

    case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hWnd;
            TrackMouseEvent(&tme);

            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            UpdateHoverState(point);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (m_hoveredControl) {
            HWND oldHovered = m_hoveredControl;
            m_hoveredControl = nullptr;
            InvalidateRect(oldHovered, nullptr, TRUE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (m_hoveredControl) {
            m_pressedControl = m_hoveredControl;
            InvalidateRect(m_pressedControl, nullptr, TRUE);
        }
        break;

    case WM_LBUTTONUP:
        if (m_pressedControl) {
            HWND oldPressed = m_pressedControl;
            m_pressedControl = nullptr;
            InvalidateRect(oldPressed, nullptr, TRUE);
        }
        break;

    case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis && mis->CtlType == ODT_MENU && IsStyledMenuItem(ResolveStyledMenuItemId(mis->itemID, mis->itemData))) {
                MeasureStyledMenuItem(mis);
                return TRUE;
            }
        }
        break;

    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!dis) {
                break;
            }
            if (dis->CtlType == ODT_MENU && IsStyledMenuItem(ResolveStyledMenuItemId(dis->itemID, dis->itemData))) {
                DrawStyledMenuItem(dis);
                return TRUE;
            }
            if (dis->CtlType != ODT_BUTTON) {
                break;
            }
            if (dis->CtlID == ID_RELOAD_BUTTON
                || dis->CtlID == ID_OPEN_FOLDER_BUTTON
                || dis->CtlID == ID_MORE_BUTTON) {
                wchar_t text[128] = {};
                GetWindowTextW(dis->hwndItem, text, 128);
                const bool pressed = m_pressedControl == dis->hwndItem || (dis->itemState & ODS_SELECTED) != 0;
                const float hoverAlpha = (m_hoveredControl == dis->hwndItem) ? 1.0f : 0.0f;
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, text, pressed, hoverAlpha);
                return TRUE;
            }
        }
        break;

    case WM_INITMENUPOPUP:
        {
            HMENU popupMenu = reinterpret_cast<HMENU>(wParam);
            if (popupMenu) {
                MENUINFO popupMenuInfo = {};
                popupMenuInfo.cbSize = sizeof(MENUINFO);
                popupMenuInfo.fMask = MIM_BACKGROUND;
                popupMenuInfo.hbrBack = m_hCardBrush;
                SetMenuInfo(popupMenu, &popupMenuInfo);
            }
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT cursorPos = {};
            GetCursorPos(&cursorPos);
            ScreenToClient(m_hWnd, &cursorPos);
            UpdateHoverState(cursorPos);
        }
        return DefWindowProcW(m_hWnd, message, wParam, lParam);

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_CLOSE:
        if (!m_isExiting) {
            HideToTray();
            return 0;
        }
        DestroyWindow(m_hWnd);
        return 0;

    case WM_CTLCOLORSTATIC:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            if (control == m_hTitleLabel) {
                SetTextColor(hdc, RGB(255, 255, 255));
                return reinterpret_cast<INT_PTR>(m_hCardBrush);
            }
            if (control == m_hStatusLabel) {
                SetTextColor(hdc, RGB(200, 200, 200));
                return reinterpret_cast<INT_PTR>(m_hCardBrush);
            }
            SetTextColor(hdc, RGB(230, 230, 230));
            return reinterpret_cast<INT_PTR>(m_hCardBrush);
        }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(37, 37, 37));
            SetTextColor(hdc, RGB(245, 245, 245));
            return reinterpret_cast<INT_PTR>(m_hListBrush);
        }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hWnd, message, wParam, lParam);
}

void Application::CreateControls() {
    m_hTitleFont = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_hFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_hMonoFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    m_hTitleLabel = CreateWindowExW(0, L"STATIC", L"TextMagic", WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 100, 30, m_hWnd, reinterpret_cast<HMENU>(ID_TITLE_LABEL), m_hInstance, nullptr);

    m_hHintLabel = CreateWindowExW(0, L"STATIC",
        L(Localization::Key::HintLabel),
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 40,
        m_hWnd, reinterpret_cast<HMENU>(ID_HINT_LABEL), m_hInstance, nullptr);

    m_hScriptList = CreateWindowExW(0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL,
        0, 0, 100, 100, m_hWnd, reinterpret_cast<HMENU>(ID_SCRIPTS_LIST), m_hInstance, nullptr);

    m_hReloadButton = CreateWindowExW(0, L"BUTTON", L(Localization::Key::ButtonReloadScripts),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 100, 32, m_hWnd, reinterpret_cast<HMENU>(ID_RELOAD_BUTTON), m_hInstance, nullptr);

    m_hOpenFolderButton = CreateWindowExW(0, L"BUTTON", L(Localization::Key::ButtonOpenScriptsFolder),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 100, 32, m_hWnd, reinterpret_cast<HMENU>(ID_OPEN_FOLDER_BUTTON), m_hInstance, nullptr);

    m_hMoreButton = CreateWindowExW(0, L"BUTTON", L(Localization::Key::ButtonMore),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 100, 32, m_hWnd, reinterpret_cast<HMENU>(ID_MORE_BUTTON), m_hInstance, nullptr);

    m_hStatusLabel = CreateWindowExW(0, L"STATIC", L(Localization::Key::StatusReady),
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24,
        m_hWnd, reinterpret_cast<HMENU>(ID_STATUS_LABEL), m_hInstance, nullptr);

    SendMessageW(m_hTitleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hTitleFont), TRUE);
    SendMessageW(m_hHintLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hScriptList, WM_SETFONT, reinterpret_cast<WPARAM>(m_hMonoFont), TRUE);
    SendMessageW(m_hReloadButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hOpenFolderButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hMoreButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hStatusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);

    m_toolTip = std::make_unique<ToolTip>();
    if (m_toolTip->Initialize(m_hWnd)) {
        m_toolTip->SetStyle(m_hFont, RGB(50, 50, 50), RGB(240, 240, 240));
        m_toolTip->AddTool(m_hReloadButton, L(Localization::Key::TooltipReload));
        m_toolTip->AddTool(m_hScriptList, L(Localization::Key::TooltipScriptList));
        m_toolTip->AddTool(m_hOpenFolderButton, L(Localization::Key::TooltipOpenScriptsFolder));
        m_toolTip->AddTool(m_hMoreButton, L(Localization::Key::TooltipMore));
    }
}

void Application::CreateMoreMenu() {
    if (m_hMoreMenu) {
        DestroyMenu(m_hMoreMenu);
        m_hMoreMenu = nullptr;
        m_hLanguageMenu = nullptr;
    }
    m_hMoreMenu = CreatePopupMenu();
    if (!m_hMoreMenu) {
        return;
    }
    m_hLanguageMenu = CreatePopupMenu();
    if (!m_hLanguageMenu) {
        DestroyMenu(m_hMoreMenu);
        m_hMoreMenu = nullptr;
        return;
    }

    AppendMenuW(m_hLanguageMenu, MF_OWNERDRAW, ID_MENU_LANGUAGE_RU, GetMenuItemText(ID_MENU_LANGUAGE_RU));
    AppendMenuW(m_hLanguageMenu, MF_OWNERDRAW, ID_MENU_LANGUAGE_EN, GetMenuItemText(ID_MENU_LANGUAGE_EN));

    MENUINFO menuInfo = {};
    menuInfo.cbSize = sizeof(MENUINFO);
    menuInfo.fMask = MIM_BACKGROUND;
    menuInfo.hbrBack = m_hCardBrush;
    SetMenuInfo(m_hMoreMenu, &menuInfo);
    SetMenuInfo(m_hLanguageMenu, &menuInfo);

    AppendMenuW(m_hMoreMenu, MF_OWNERDRAW, ID_MENU_MORE_LOGS, GetMenuItemText(ID_MENU_MORE_LOGS));
    AppendMenuW(m_hMoreMenu, MF_OWNERDRAW, ID_MENU_MORE_SEPARATOR, L"");
    AppendMenuW(m_hMoreMenu, MF_OWNERDRAW, ID_MENU_LANGUAGE_LABEL, GetMenuItemText(ID_MENU_LANGUAGE_LABEL));
    AppendMenuW(m_hMoreMenu, MF_OWNERDRAW, ID_MENU_MORE_SEPARATOR, L"");
    AppendMenuW(m_hMoreMenu, MF_OWNERDRAW, ID_MENU_MORE_ABOUT, GetMenuItemText(ID_MENU_MORE_ABOUT));
    UpdateLanguageMenuChecks();
}

void Application::OnResize(int width, int height) {
    const int outerMargin = 14;
    const int statusCardHeight = 44;
    const int cardGap = 8;

    m_statusCardRect.left = outerMargin;
    m_statusCardRect.right = width - outerMargin;
    m_statusCardRect.bottom = height - outerMargin;
    m_statusCardRect.top = m_statusCardRect.bottom - statusCardHeight;

    m_cardRect.left = outerMargin;
    m_cardRect.top = outerMargin;
    m_cardRect.right = width - outerMargin;
    m_cardRect.bottom = m_statusCardRect.top - cardGap;

    const int innerX = m_cardRect.left + 22;
    const int innerY = m_cardRect.top + 16;
    const int innerWidth = std::max(120, static_cast<int>((m_cardRect.right - m_cardRect.left) - 44));

    MoveWindow(m_hTitleLabel, innerX, innerY + 6, innerWidth, 34, TRUE);
    MoveWindow(m_hHintLabel, innerX, innerY + 46, innerWidth, 48, TRUE);

    const int buttonHeight = 36;
    const int buttonGap = 10;
    const int buttonCount = 3;
    const int buttonWidth = std::max(84, (innerWidth - buttonGap * (buttonCount - 1)) / buttonCount);
    const int buttonRowY = std::max(innerY + 180, static_cast<int>(m_cardRect.bottom - 58));

    const int listTop = innerY + 102;
    const int listHeight = std::max(90, buttonRowY - listTop - 14);
    MoveWindow(m_hScriptList, innerX, listTop, innerWidth, listHeight, TRUE);

    const int buttonsTotalWidth = buttonCount * buttonWidth + buttonGap * (buttonCount - 1);
    int x = innerX + std::max(0, (innerWidth - buttonsTotalWidth) / 2);
    MoveWindow(m_hReloadButton, x, buttonRowY, buttonWidth, buttonHeight, TRUE);
    x += buttonWidth + buttonGap;
    MoveWindow(m_hOpenFolderButton, x, buttonRowY, buttonWidth, buttonHeight, TRUE);
    x += buttonWidth + buttonGap;
    MoveWindow(m_hMoreButton, x, buttonRowY, buttonWidth, buttonHeight, TRUE);

    const int statusLabelX = m_statusCardRect.left + 22;
    const int statusLabelY = m_statusCardRect.top + 10;
    const int statusLabelWidth = std::max(120, static_cast<int>((m_statusCardRect.right - m_statusCardRect.left) - 44));
    MoveWindow(m_hStatusLabel, statusLabelX, statusLabelY, statusLabelWidth, 24, TRUE);
}

void Application::OnPaint() {
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(m_hWnd, &ps);
    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    UiRenderer::DrawBackground(hdc, clientRect);
    UiRenderer::DrawCard(hdc, m_cardRect);
    UiRenderer::DrawCard(hdc, m_statusCardRect);
    UiRenderer::DrawEditBorder(m_hWnd, m_hScriptList);
    EndPaint(m_hWnd, &ps);
}

void Application::OnCommand(UINT controlId, UINT notifyCode) {
    switch (controlId) {
    case ID_RELOAD_BUTTON:
        if (notifyCode == BN_CLICKED) {
            ReloadScripts(true);
        }
        break;
    case ID_OPEN_FOLDER_BUTTON:
        if (notifyCode == BN_CLICKED) {
            OpenScriptsFolder();
        }
        break;
    case ID_MORE_BUTTON:
        if (notifyCode == BN_CLICKED) {
            ShowMoreMenu();
        }
        break;
    case ID_SCRIPTS_LIST:
        if (notifyCode == LBN_DBLCLK) {
            ExecuteSelectedScript();
        }
        break;
    default:
        break;
    }
}

void Application::OnMenuCommand(UINT menuId) {
    switch (menuId) {
    case ID_MENU_MORE_ABOUT:
        ShowAboutWindow();
        break;
    case ID_MENU_MORE_LOGS:
        ShowLogsWindow();
        break;
    case ID_MENU_SCRIPTS_ADD:
        AddScriptViaDialog();
        break;
    case ID_MENU_SCRIPTS_ENABLE:
        SetSelectedScriptsEnabled(true);
        break;
    case ID_MENU_SCRIPTS_DISABLE:
        SetSelectedScriptsEnabled(false);
        break;
    case ID_MENU_SCRIPTS_DELETE:
        RemoveSelectedScripts();
        break;
    case ID_MENU_SCRIPTS_IMPORT_ZIP:
        ImportScriptsFromZip();
        break;
    case ID_MENU_SCRIPTS_EXPORT_ZIP:
        ExportScriptsToZip();
        break;
    case ID_MENU_LANGUAGE_LABEL:
        {
            POINT cursor = {};
            GetCursorPos(&cursor);
            cursor.x += 14;
            cursor.y -= 10;
            ShowLanguageMenuPopup(cursor, false);
        }
        break;
    case ID_MENU_LANGUAGE_RU:
        SetLanguage(false);
        break;
    case ID_MENU_LANGUAGE_EN:
        SetLanguage(true);
        break;
    case ID_MENU_TRAY_EXIT:
        ExitApplication();
        break;
    default:
        break;
    }
}

void Application::ShowLanguageMenuPopup(POINT screenPoint, bool recurse) {
    if (!m_hLanguageMenu || m_languageMenuPopupOpen) {
        return;
    }

    m_languageMenuPopupOpen = true;
    m_languageMenuLastOpenTick = GetTickCount();
    UpdateLanguageMenuChecks();
    if (!recurse) {
        SetForegroundWindow(m_hWnd);
    }

    if (recurse) {
        TrackPopupMenuEx(
            m_hLanguageMenu,
            TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RECURSE,
            screenPoint.x,
            screenPoint.y,
            m_hWnd,
            nullptr
        );
        m_languageMenuPopupOpen = false;
        return;
    }

    const UINT languageCommand = TrackPopupMenuEx(
        m_hLanguageMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        screenPoint.x,
        screenPoint.y,
        m_hWnd,
        nullptr
    );

    m_languageMenuPopupOpen = false;
    if (languageCommand != 0) {
        SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(languageCommand, 0), 0);
    }
}

void Application::ShowMoreMenu() {
    if (!m_hMoreMenu) {
        return;
    }
    m_moreMenuTracking = true;
    UpdateLanguageMenuChecks();
    RECT rect = {};
    GetWindowRect(m_hMoreButton, &rect);
    TrackPopupMenu(m_hMoreMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        rect.left, rect.bottom + 2, 0, m_hWnd, nullptr);
    m_moreMenuTracking = false;
}

void Application::ApplyLocalization() {
    if (m_hHintLabel) {
        SetWindowTextW(m_hHintLabel, L(Localization::Key::HintLabel));
    }
    if (m_hReloadButton) {
        SetWindowTextW(m_hReloadButton, L(Localization::Key::ButtonReloadScripts));
    }
    if (m_hOpenFolderButton) {
        SetWindowTextW(m_hOpenFolderButton, L(Localization::Key::ButtonOpenScriptsFolder));
    }
    if (m_hMoreButton) {
        SetWindowTextW(m_hMoreButton, L(Localization::Key::ButtonMore));
    }

    wchar_t statusText[256] = {};
    if (m_hStatusLabel) {
        GetWindowTextW(m_hStatusLabel, statusText, static_cast<int>(_countof(statusText)));
    }
    if (statusText[0] == L'\0' || wcscmp(statusText, L"Готово.") == 0 || wcscmp(statusText, L"Ready.") == 0) {
        SetStatusText(L(Localization::Key::StatusReady));
    }

    if (m_toolTip) {
        m_toolTip->AddTool(m_hReloadButton, L(Localization::Key::TooltipReload));
        m_toolTip->AddTool(m_hScriptList, L(Localization::Key::TooltipScriptList));
        m_toolTip->AddTool(m_hOpenFolderButton, L(Localization::Key::TooltipOpenScriptsFolder));
        m_toolTip->AddTool(m_hMoreButton, L(Localization::Key::TooltipMore));
    }

    CreateMoreMenu();
    if (m_hAboutWindow && IsWindow(m_hAboutWindow)) {
        SetWindowTextW(m_hAboutWindow, GetMenuItemText(ID_MENU_MORE_ABOUT));
        UpdateInfoWindowText(InfoWindowKind::About, BuildAboutText());
    }
    if (m_hLogsWindow && IsWindow(m_hLogsWindow)) {
        SetWindowTextW(m_hLogsWindow, GetMenuItemText(ID_MENU_MORE_LOGS));
        UpdateInfoWindowText(InfoWindowKind::Logs, BuildLogText());
    }
}

void Application::SetLanguage(bool useEnglish) {
    const Localization::Language newLanguage = useEnglish ? Localization::Language::English : Localization::Language::Russian;
    if (g_currentLanguage == newLanguage) {
        return;
    }
    g_currentLanguage = newLanguage;
    ApplyLocalization();
    RefreshScriptList();
    SetStatusText(L(Localization::Key::StatusLanguageUpdated));
}

void Application::UpdateLanguageMenuChecks() {
    if (!m_hLanguageMenu) {
        return;
    }
    CheckMenuRadioItem(
        m_hLanguageMenu,
        ID_MENU_LANGUAGE_RU,
        ID_MENU_LANGUAGE_EN,
        g_currentLanguage == Localization::Language::English ? ID_MENU_LANGUAGE_EN : ID_MENU_LANGUAGE_RU,
        MF_BYCOMMAND
    );
}

bool Application::InitializeTrayIcon() {
    if (!m_hWnd || m_trayIconData.cbSize != 0) {
        return false;
    }

    ZeroMemory(&m_trayIconData, sizeof(m_trayIconData));
    m_trayIconData.cbSize = sizeof(m_trayIconData);
    m_trayIconData.hWnd = m_hWnd;
    m_trayIconData.uID = TRAY_ICON_ID;
    m_trayIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_trayIconData.uCallbackMessage = WM_TRAYICON;
    m_trayIconData.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    if (!m_trayIconData.hIcon) {
        m_trayIconData.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(m_trayIconData.szTip, WINDOW_TITLE);

    if (!Shell_NotifyIconW(NIM_ADD, &m_trayIconData)) {
        ZeroMemory(&m_trayIconData, sizeof(m_trayIconData));
        return false;
    }

    m_trayIconData.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &m_trayIconData);
    return true;
}

void Application::RemoveTrayIcon() {
    if (m_trayIconData.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &m_trayIconData);
        ZeroMemory(&m_trayIconData, sizeof(m_trayIconData));
    }
}

void Application::ShowTrayContextMenu(POINT screenPoint) {
    HMENU trayMenu = CreatePopupMenu();
    if (!trayMenu) {
        return;
    }

    AppendMenuW(trayMenu, MF_OWNERDRAW, ID_MENU_TRAY_EXIT, GetMenuItemText(ID_MENU_TRAY_EXIT));

    MENUINFO menuInfo = {};
    menuInfo.cbSize = sizeof(MENUINFO);
    menuInfo.fMask = MIM_BACKGROUND;
    menuInfo.hbrBack = m_hCardBrush;
    SetMenuInfo(trayMenu, &menuInfo);

    SetForegroundWindow(m_hWnd);
    const UINT command = TrackPopupMenu(
        trayMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        screenPoint.x,
        screenPoint.y,
        0,
        m_hWnd,
        nullptr
    );
    if (command != 0) {
        SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
    DestroyMenu(trayMenu);
}

void Application::HideToTray() {
    if (!m_hWnd || !IsWindow(m_hWnd)) {
        return;
    }
    ShowWindow(m_hWnd, SW_HIDE);
    m_hiddenToTray = true;
}

void Application::RestoreFromTray() {
    if (!m_hWnd || !IsWindow(m_hWnd)) {
        return;
    }
    ShowWindow(m_hWnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_hWnd);
    m_hiddenToTray = false;
}

void Application::ExitApplication() {
    m_isExiting = true;
    if (m_hWnd && IsWindow(m_hWnd)) {
        PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
    }
}

void Application::ShowScriptListContextMenu(POINT screenPoint) {
    if (!m_hScriptList || !IsWindow(m_hScriptList)) {
        return;
    }

    const std::vector<size_t> selectedIndices = GetSelectedScriptIndices();
    const bool hasSelection = !selectedIndices.empty();

    HMENU contextMenu = CreatePopupMenu();
    if (!contextMenu) {
        return;
    }

    AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_SCRIPTS_ADD, GetMenuItemText(ID_MENU_SCRIPTS_ADD));
    AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_SCRIPTS_IMPORT_ZIP, GetMenuItemText(ID_MENU_SCRIPTS_IMPORT_ZIP));
    AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_SCRIPTS_EXPORT_ZIP, GetMenuItemText(ID_MENU_SCRIPTS_EXPORT_ZIP));
    AppendMenuW(contextMenu, MF_OWNERDRAW, ID_MENU_MORE_SEPARATOR, GetMenuItemText(ID_MENU_MORE_SEPARATOR));
    AppendMenuW(contextMenu, hasSelection ? MF_OWNERDRAW : (MF_OWNERDRAW | MF_GRAYED), ID_MENU_SCRIPTS_ENABLE, GetMenuItemText(ID_MENU_SCRIPTS_ENABLE));
    AppendMenuW(contextMenu, hasSelection ? MF_OWNERDRAW : (MF_OWNERDRAW | MF_GRAYED), ID_MENU_SCRIPTS_DISABLE, GetMenuItemText(ID_MENU_SCRIPTS_DISABLE));
    AppendMenuW(contextMenu, hasSelection ? MF_OWNERDRAW : (MF_OWNERDRAW | MF_GRAYED), ID_MENU_SCRIPTS_DELETE, GetMenuItemText(ID_MENU_SCRIPTS_DELETE));

    MENUINFO menuInfo = {};
    menuInfo.cbSize = sizeof(MENUINFO);
    menuInfo.fMask = MIM_BACKGROUND;
    menuInfo.hbrBack = m_hCardBrush;
    SetMenuInfo(contextMenu, &menuInfo);

    SetForegroundWindow(m_hWnd);
    const UINT command = TrackPopupMenu(
        contextMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        screenPoint.x,
        screenPoint.y,
        0,
        m_hWnd,
        nullptr
    );
    if (command != 0) {
        SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
    DestroyMenu(contextMenu);
}

void Application::ReloadScripts(bool announceResult) {
    AppendLog(L"[Scripts] Перезагрузка манифестов из " + m_scriptsDirectory + L".");

    UnregisterHotkeys();
    m_scripts.clear();
    m_scriptIndexByHotkeyId.clear();

    const ScriptManifest::LoadResult loadResult = ScriptManifest::LoadFromDirectory(m_scriptsDirectory);
    int counter = 0;
    m_scripts.reserve(loadResult.entries.size());
    for (const auto& entry : loadResult.entries) {
        RegisteredScript script;
        script.manifest = entry;
        script.hotkeyId = HOTKEY_BASE + counter;
        m_scripts.push_back(std::move(script));
        ++counter;
    }

    RegisterHotkeys();
    RefreshScriptList();

    if (!loadResult.warning.empty()) {
        AppendLog(L"[Scripts][Warning] " + loadResult.warning);
        OutputDebugStringW(loadResult.warning.c_str());
    }

    if (!announceResult) {
        return;
    }
    if (m_scripts.empty()) {
        SetStatusText(L(Localization::Key::StatusNoScriptsFound));
        AppendLog(L"[Scripts] Скрипты не найдены.");
        return;
    }

    size_t registeredCount = 0;
    for (const auto& script : m_scripts) {
        if (script.hotkeyRegistered) {
            ++registeredCount;
        }
    }

    const std::wstring status = L"Скриптов: " + std::to_wstring(m_scripts.size())
        + L". Горячих клавиш активно: " + std::to_wstring(registeredCount) + L".";
    SetStatusText(status);
    AppendLog(L"[Scripts] " + status);
}

void Application::RegisterHotkeys() {
    for (auto& script : m_scripts) {
        script.hotkeyRegistered = false;
        script.hotkeyError.clear();
        if (!script.manifest.enabled) {
            script.hotkeyError = L"Отключен пользователем";
            continue;
        }
        if (script.manifest.virtualKey == 0) {
            script.hotkeyError = L"Неверный хоткей";
            continue;
        }

        const UINT modifiers = script.manifest.modifiers | MOD_NOREPEAT;
        if (!RegisterHotKey(m_hWnd, script.hotkeyId, modifiers, script.manifest.virtualKey)) {
            script.hotkeyError = L"RegisterHotKey: " + std::to_wstring(GetLastError());
            AppendLog(L"[Hotkey][Ошибка] " + script.manifest.name + L" [" + script.manifest.hotkeyText + L"]: " + script.hotkeyError);
            continue;
        }

        script.hotkeyRegistered = true;
        m_scriptIndexByHotkeyId[script.hotkeyId] = &script - m_scripts.data();
    }
}

void Application::UnregisterHotkeys() {
    for (const auto& script : m_scripts) {
        if (script.hotkeyRegistered) {
            UnregisterHotKey(m_hWnd, script.hotkeyId);
        }
    }
    m_scriptIndexByHotkeyId.clear();
}

void Application::RefreshScriptList() {
    SendMessageW(m_hScriptList, LB_RESETCONTENT, 0, 0);
    for (const auto& script : m_scripts) {
        std::wstring line = script.manifest.enabled ? L"[ON] " : L"[OFF] ";
        line += script.manifest.name + L" [" + script.manifest.hotkeyText + L"]";
        if (!script.manifest.description.empty()) {
            line += L" - " + script.manifest.description;
        }
        if (script.manifest.enabled && !script.hotkeyRegistered) {
            line += L(Localization::Key::ScriptListHotkeyUnavailablePrefix);
            line += script.hotkeyError + L")";
        }
        SendMessageW(m_hScriptList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (!m_scripts.empty()) {
        SendMessageW(m_hScriptList, LB_SETCURSEL, 0, 0);
    }
}

std::vector<size_t> Application::GetSelectedScriptIndices() const {
    std::vector<size_t> result;
    const LRESULT selectedCount = SendMessageW(m_hScriptList, LB_GETSELCOUNT, 0, 0);
    if (selectedCount == LB_ERR || selectedCount <= 0) {
        return result;
    }

    std::vector<int> selectedItems(static_cast<size_t>(selectedCount), 0);
    const LRESULT copiedCount = SendMessageW(
        m_hScriptList,
        LB_GETSELITEMS,
        static_cast<WPARAM>(selectedItems.size()),
        reinterpret_cast<LPARAM>(selectedItems.data())
    );
    if (copiedCount <= 0) {
        return result;
    }

    result.reserve(static_cast<size_t>(copiedCount));
    for (LRESULT i = 0; i < copiedCount; ++i) {
        const int value = selectedItems[static_cast<size_t>(i)];
        if (value >= 0 && static_cast<size_t>(value) < m_scripts.size()) {
            result.push_back(static_cast<size_t>(value));
        }
    }
    return result;
}

bool Application::GetPrimarySelectedScriptIndex(size_t* selectedIndex) const {
    if (selectedIndex) {
        *selectedIndex = 0;
    }

    const std::vector<size_t> selected = GetSelectedScriptIndices();
    if (!selected.empty()) {
        if (selectedIndex) {
            *selectedIndex = selected.front();
        }
        return true;
    }

    const LRESULT cursorIndex = SendMessageW(m_hScriptList, LB_GETCURSEL, 0, 0);
    if (cursorIndex == LB_ERR || cursorIndex < 0 || static_cast<size_t>(cursorIndex) >= m_scripts.size()) {
        return false;
    }
    if (selectedIndex) {
        *selectedIndex = static_cast<size_t>(cursorIndex);
    }
    return true;
}

void Application::SetSelectedScriptsEnabled(bool enabled) {
    const std::vector<size_t> selectedIndices = GetSelectedScriptIndices();
    if (selectedIndices.empty()) {
        SetStatusText(L"Выберите один или несколько скриптов.");
        return;
    }

    std::vector<std::wstring> selectedPaths;
    selectedPaths.reserve(selectedIndices.size());

    int updatedCount = 0;
    int failedCount = 0;
    for (size_t index : selectedIndices) {
        if (index >= m_scripts.size()) {
            continue;
        }

        const auto& script = m_scripts[index];
        selectedPaths.push_back(script.manifest.manifestPath);
        if (script.manifest.enabled == enabled) {
            continue;
        }

        std::wstring updateError;
        if (!ScriptManifest::SetEnabledInFile(script.manifest.manifestPath, enabled, &updateError)) {
            ++failedCount;
            AppendLog(L"[Scripts][Ошибка] Не удалось обновить enabled для \"" + script.manifest.name + L"\": " + updateError);
            continue;
        }
        ++updatedCount;
    }

    if (updatedCount > 0) {
        ReloadScripts(false);

        SendMessageW(m_hScriptList, LB_SETSEL, FALSE, -1);
        for (size_t i = 0; i < m_scripts.size(); ++i) {
            for (const auto& path : selectedPaths) {
                if (_wcsicmp(m_scripts[i].manifest.manifestPath.c_str(), path.c_str()) == 0) {
                    SendMessageW(m_hScriptList, LB_SETSEL, TRUE, static_cast<LPARAM>(i));
                    break;
                }
            }
        }
    }

    const std::wstring actionText = enabled ? L"включено" : L"отключено";
    const std::wstring status = L"Скриптов " + actionText + L": " + std::to_wstring(updatedCount)
        + L". Ошибок: " + std::to_wstring(failedCount) + L".";
    SetStatusText(status);
    AppendLog(L"[Scripts] " + status);
}

void Application::AddScriptViaDialog() {
    wchar_t filePath[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"TextMagic scripts (*.tmscript)\0*.tmscript\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) {
        const DWORD dialogError = CommDlgExtendedError();
        if (dialogError != 0) {
            const std::wstring error = L"Ошибка диалога выбора файла: " + std::to_wstring(dialogError);
            SetStatusText(error);
            AppendLog(L"[Scripts][Ошибка] " + error);
        }
        return;
    }

    ImportScriptFiles({ filePath });
}

void Application::RemoveSelectedScripts() {
    const std::vector<size_t> selectedIndices = GetSelectedScriptIndices();
    if (selectedIndices.empty()) {
        SetStatusText(L"Выберите один или несколько скриптов.");
        return;
    }

    std::wstring prompt = L"Удалить выбранные скрипты: " + std::to_wstring(selectedIndices.size()) + L" шт.?\r\n\r\n";
    const size_t previewCount = std::min<size_t>(selectedIndices.size(), 5);
    for (size_t i = 0; i < previewCount; ++i) {
        const size_t index = selectedIndices[i];
        if (index < m_scripts.size()) {
            prompt += L"- " + m_scripts[index].manifest.name + L"\r\n";
        }
    }
    if (selectedIndices.size() > previewCount) {
        prompt += L"...";
    }
    const int answer = ShowStyledMessageDialog(L"Удаление скрипта", prompt, L"Удалить", L"Отмена");
    if (answer != IDYES) {
        return;
    }

    int removedCount = 0;
    int failedCount = 0;
    std::wstring firstErrorPath;
    for (size_t index : selectedIndices) {
        if (index >= m_scripts.size()) {
            continue;
        }
        const std::wstring path = m_scripts[index].manifest.manifestPath;
        std::error_code removeError;
        const bool removed = fs::remove(fs::path(path), removeError);
        if (!removed || removeError) {
            ++failedCount;
            if (firstErrorPath.empty()) {
                firstErrorPath = path;
            }
            AppendLog(L"[Scripts][Ошибка] Не удалось удалить файл: " + path + L". Код: " + std::to_wstring(removeError.value()));
            continue;
        }
        ++removedCount;
    }

    if (removedCount > 0) {
        ReloadScripts(false);
    }

    const std::wstring status = L"Удалено скриптов: " + std::to_wstring(removedCount)
        + L". Ошибок: " + std::to_wstring(failedCount) + L".";
    SetStatusText(status);
    AppendLog(L"[Scripts] " + status);
    if (failedCount > 0 && !firstErrorPath.empty()) {
        ShowStyledMessage(L"Ошибка удаления", L"Не все файлы удалось удалить.\r\n\r\n" + firstErrorPath);
    }
}

void Application::ImportScriptsFromZip() {
    wchar_t archivePath[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"ZIP архивы (*.zip)\0*.zip\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = archivePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    wchar_t tempDirectory[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDirectory)) {
        ShowStyledMessage(L"Ошибка импорта", L"Не удалось получить путь к временной папке.");
        return;
    }

    wchar_t tempName[MAX_PATH] = {};
    if (!GetTempFileNameW(tempDirectory, L"tmz", 0, tempName)) {
        ShowStyledMessage(L"Ошибка импорта", L"Не удалось создать временный путь.");
        return;
    }
    DeleteFileW(tempName);

    if (!CreateDirectoryW(tempName, nullptr)) {
        ShowStyledMessage(L"Ошибка импорта", L"Не удалось создать временную папку.");
        return;
    }

    const std::wstring escapedArchive = EscapePowerShellSingleQuoted(archivePath);
    const std::wstring escapedTempDir = EscapePowerShellSingleQuoted(tempName);
    const std::wstring extractScript =
        L"$ErrorActionPreference='Stop'\n"
        L"$archivePath='" + escapedArchive + L"'\n"
        L"$destinationPath='" + escapedTempDir + L"'\n"
        L"Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath -Force\n";

    std::wstring ignoredOutput;
    std::wstring executeError;
    if (!m_scriptRunner.ExecutePowerShellScript(extractScript, L"", &ignoredOutput, &executeError)) {
        std::error_code cleanupError;
        fs::remove_all(fs::path(tempName), cleanupError);
        ShowStyledMessage(L"Ошибка импорта", L"Не удалось распаковать ZIP архив.\r\n\r\n" + executeError);
        return;
    }

    std::vector<std::wstring> scriptFiles;
    std::error_code walkError;
    for (const auto& entry : fs::recursive_directory_iterator(fs::path(tempName), walkError)) {
        if (walkError) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (IsTmscriptFilePath(entry.path())) {
            scriptFiles.push_back(entry.path().wstring());
        }
    }

    ImportScriptFiles(scriptFiles);

    std::error_code cleanupError;
    fs::remove_all(fs::path(tempName), cleanupError);
}

void Application::ExportScriptsToZip() {
    std::error_code walkError;
    int scriptFileCount = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(m_scriptsDirectory), walkError)) {
        if (walkError) {
            break;
        }
        if (entry.is_regular_file() && IsTmscriptFilePath(entry.path())) {
            ++scriptFileCount;
        }
    }
    if (scriptFileCount == 0) {
        SetStatusText(L"Нет скриптов для экспорта.");
        return;
    }

    wchar_t archivePath[MAX_PATH] = {};
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t defaultName[128] = {};
    swprintf_s(defaultName, L"TextMagic-scripts-%04u%02u%02u-%02u%02u%02u.zip",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wcscpy_s(archivePath, defaultName);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"ZIP архивы (*.zip)\0*.zip\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"zip";
    ofn.lpstrFile = archivePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    const std::wstring escapedScriptsDir = EscapePowerShellSingleQuoted(m_scriptsDirectory);
    const std::wstring escapedArchive = EscapePowerShellSingleQuoted(archivePath);
    const std::wstring exportScript =
        L"$ErrorActionPreference='Stop'\n"
        L"$scriptsDir='" + escapedScriptsDir + L"'\n"
        L"$destinationPath='" + escapedArchive + L"'\n"
        L"$files=Get-ChildItem -LiteralPath $scriptsDir -Filter '*.tmscript' -File\n"
        L"if(-not $files){ throw 'Нет .tmscript файлов для экспорта.' }\n"
        L"Compress-Archive -LiteralPath $files.FullName -DestinationPath $destinationPath -Force\n";

    std::wstring ignoredOutput;
    std::wstring executeError;
    if (!m_scriptRunner.ExecutePowerShellScript(exportScript, L"", &ignoredOutput, &executeError)) {
        ShowStyledMessage(L"Ошибка экспорта", L"Не удалось создать ZIP архив.\r\n\r\n" + executeError);
        return;
    }

    const std::wstring status = L"Экспортировано " + std::to_wstring(scriptFileCount) + L" скриптов в: " + std::wstring(archivePath);
    SetStatusText(status);
    AppendLog(L"[Scripts] " + status);
}

void Application::ImportScriptFiles(const std::vector<std::wstring>& filePaths) {
    if (filePaths.empty()) {
        return;
    }

    int importedCount = 0;
    int skippedCount = 0;
    for (const auto& filePath : filePaths) {
        std::wstring copiedPath;
        if (ImportScriptFile(filePath, &copiedPath)) {
            ++importedCount;
            AppendLog(L"[Scripts] Импортирован файл: " + copiedPath);
        } else {
            ++skippedCount;
        }
    }

    if (importedCount > 0) {
        ReloadScripts(false);
        const std::wstring status =
            L"Добавлено скриптов: " + std::to_wstring(importedCount) + L". Пропущено: " + std::to_wstring(skippedCount) + L".";
        SetStatusText(status);
        AppendLog(L"[Scripts] " + status);
        return;
    }

    const std::wstring status = L"Подходящие .tmscript не найдены для добавления.";
    SetStatusText(status);
    AppendLog(L"[Scripts] " + status);
}

bool Application::ImportScriptFile(const std::wstring& sourcePath, std::wstring* copiedPath) const {
    if (copiedPath) {
        copiedPath->clear();
    }

    const fs::path sourceFile(sourcePath);
    std::error_code statError;
    if (!fs::is_regular_file(sourceFile, statError) || statError) {
        return false;
    }
    if (!IsTmscriptFilePath(sourceFile)) {
        return false;
    }

    std::error_code createDirError;
    fs::create_directories(fs::path(m_scriptsDirectory), createDirError);
    if (createDirError) {
        return false;
    }

    fs::path destination = fs::path(m_scriptsDirectory) / sourceFile.filename();
    std::error_code equivalentError;
    if (fs::exists(destination) && fs::equivalent(sourceFile, destination, equivalentError) && !equivalentError) {
        return false;
    }

    if (fs::exists(destination)) {
        const std::wstring stem = destination.stem().wstring();
        const std::wstring ext = destination.extension().wstring();
        int suffix = 1;
        while (fs::exists(destination)) {
            destination = fs::path(m_scriptsDirectory) / (stem + L"_" + std::to_wstring(suffix) + ext);
            ++suffix;
        }
    }

    std::error_code copyError;
    fs::copy_file(sourceFile, destination, fs::copy_options::none, copyError);
    if (copyError) {
        return false;
    }

    if (copiedPath) {
        *copiedPath = destination.wstring();
    }
    return true;
}

void Application::ExecuteSelectedScript() {
    size_t selectedIndex = 0;
    if (!GetPrimarySelectedScriptIndex(&selectedIndex)) {
        SetStatusText(L"Выберите скрипт из списка.");
        return;
    }
    if (!m_scripts[selectedIndex].manifest.enabled) {
        SetStatusText(L"Скрипт отключен. Включите его перед запуском.");
        return;
    }
    ExecuteScript(m_scripts[selectedIndex]);
}

void Application::ExecuteScriptByHotkeyId(int hotkeyId) {
    const auto it = m_scriptIndexByHotkeyId.find(hotkeyId);
    if (it == m_scriptIndexByHotkeyId.end()) {
        return;
    }
    if (it->second >= m_scripts.size()) {
        return;
    }
    if (!m_scripts[it->second].manifest.enabled) {
        return;
    }
    ExecuteScript(m_scripts[it->second]);
}

void Application::ExecuteScript(const RegisteredScript& script) {
    SetStatusText(L"Выполняется: " + script.manifest.name);
    AppendLog(L"[Script] Запуск: \"" + script.manifest.name + L"\".");
    if (!script.manifest.scriptBody.empty()) {
        AppendLog(L"[Script] Режим запуска: inline PowerShell из .tmscript.");
    } else {
        AppendLog(L"[Script] Команда: " + script.manifest.commandLine);
    }

    std::wstring selectedText = m_textBridge.GetSelectedText();
    bool hasSelection = !selectedText.empty();
    bool fallbackSelectionMode = false;
    bool clipboardFallbackMode = false;
    std::wstring sourceText = hasSelection ? selectedText : m_textBridge.GetAllText();

    if (!hasSelection && sourceText.empty()) {
        std::wstring fallbackSelectedText = m_textBridge.GetSelectedText();
        if (!fallbackSelectedText.empty()) {
            sourceText = fallbackSelectedText;
            hasSelection = true;
            fallbackSelectionMode = true;
            AppendLog(L"[Script] Фолбэк: текст получен повторным чтением выделения.");
        }
    }

    if (sourceText.empty()) {
        for (int attempt = 0; attempt < 5 && sourceText.empty(); ++attempt) {
            const std::wstring clipboardText = ReadTextFromClipboard(m_hWnd);
            if (!clipboardText.empty()) {
                sourceText = clipboardText;
                clipboardFallbackMode = true;
                AppendLog(L"[Script] Фолбэк: текст получен из буфера обмена.");
                break;
            }
            Sleep(15);
        }
    }

    std::wstring sourceName = hasSelection ? L"выделение" : L"весь текст";
    if (clipboardFallbackMode) {
        sourceName = L"буфер обмена";
    }
    AppendLog(L"[Script] Источник: " + sourceName + L", символов: " + std::to_wstring(sourceText.size()) + L".");
    if (sourceText.empty()) {
        const std::wstring msg = L"Ни активное поле, ни буфер обмена не дали текст для обработки.";
        SetStatusText(msg);
        AppendLog(L"[Script][Ошибка] " + msg);
        return;
    }

    std::wstring outputText;
    std::wstring executionError;
    bool executeOk = false;
    if (!script.manifest.scriptBody.empty()) {
        executeOk = m_scriptRunner.ExecutePowerShellScript(script.manifest.scriptBody, sourceText, &outputText, &executionError);
    } else {
        executeOk = m_scriptRunner.Execute(script.manifest.commandLine, sourceText, &outputText, &executionError);
    }
    if (!executeOk) {
        const std::wstring statusMessage = L"Ошибка скрипта: " + script.manifest.name;
        const std::wstring dialogMessage = L"Скрипт: " + script.manifest.name
            + L"\r\n\r\nОшибка:\r\n" + executionError;
        SetStatusText(statusMessage);
        AppendLog(L"[Script][Ошибка] \"" + script.manifest.name + L"\": " + executionError);
        if (hasSelection || fallbackSelectionMode) {
            m_textBridge.CollapseSelection();
        }
        ShowStyledMessage(L"Ошибка выполнения", dialogMessage);
        return;
    }

    bool replaceOk = false;
    if (clipboardFallbackMode) {
        replaceOk = CopyTextToClipboard(m_hWnd, outputText);
    } else {
        replaceOk = hasSelection ? m_textBridge.SetSelectedText(outputText) : m_textBridge.SetAllText(outputText);
    }
    if (!replaceOk) {
        const std::wstring msg = clipboardFallbackMode
            ? L"Не удалось записать результат в буфер обмена."
            : L"Не удалось вставить результат в активное поле.";
        SetStatusText(msg);
        AppendLog(L"[Script][Ошибка] \"" + script.manifest.name + L"\": " + msg);
        ShowStyledMessage(L"Ошибка вставки", msg);
        return;
    }

    std::wstring status = L"Скрипт \"" + script.manifest.name + L"\" применен к ";
    if (clipboardFallbackMode) {
        status += L"тексту из буфера обмена.";
    } else {
        status += hasSelection ? L"выделенному тексту." : L"всему тексту.";
    }
    SetStatusText(status);
    AppendLog(L"[Script] Готово: \"" + script.manifest.name + L"\". Результат: "
        + std::to_wstring(outputText.size()) + L" символов.");
    AppendLog(L"[Script] " + status);
}

void Application::SetStatusText(const std::wstring& text) {
    if (m_hStatusLabel) {
        SetWindowTextW(m_hStatusLabel, text.c_str());
    }
}

void Application::OpenScriptsFolder() const {
    std::error_code createDirError;
    fs::create_directories(fs::path(m_scriptsDirectory), createDirError);
    ShellExecuteW(m_hWnd, L"open", m_scriptsDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring Application::GetExecutableDirectory() const {
    const fs::path p(GetExecutablePath());
    return p.has_parent_path() ? p.parent_path().wstring() : L".";
}

std::wstring Application::GetExecutablePath() const {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return L".\\TextMagic.exe";
    }
    return path;
}

void Application::UpdateHoverState(POINT clientPoint) {
    HWND hovered = nullptr;
    if (IsPointInControl(m_hReloadButton, clientPoint)) {
        hovered = m_hReloadButton;
    } else if (IsPointInControl(m_hOpenFolderButton, clientPoint)) {
        hovered = m_hOpenFolderButton;
    } else if (IsPointInControl(m_hMoreButton, clientPoint)) {
        hovered = m_hMoreButton;
    }

    if (hovered == m_hoveredControl) {
        return;
    }

    HWND oldHovered = m_hoveredControl;
    m_hoveredControl = hovered;
    if (oldHovered) {
        InvalidateRect(oldHovered, nullptr, TRUE);
    }
    if (m_hoveredControl) {
        InvalidateRect(m_hoveredControl, nullptr, TRUE);
    }
}

bool Application::IsPointInControl(HWND control, POINT clientPoint) const {
    if (!control || !IsWindow(control)) {
        return false;
    }
    RECT rect = {};
    GetWindowRect(control, &rect);
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&rect.left));
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&rect.right));
    return PtInRect(&rect, clientPoint) != FALSE;
}

bool Application::RegisterInfoWindowClass() {
    if (m_infoWindowClassRegistered) {
        return true;
    }

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = InfoWindowProc;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = m_hBackgroundBrush;
    wcex.lpszClassName = INFO_WINDOW_CLASS_NAME;
    wcex.hIconSm = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));

    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_infoWindowClassRegistered = true;
    return true;
}

bool Application::RegisterMessageWindowClass() {
    if (m_messageWindowClassRegistered) {
        return true;
    }

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MessageWindowProc;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = m_hBackgroundBrush;
    wcex.lpszClassName = MESSAGE_WINDOW_CLASS_NAME;
    wcex.hIconSm = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));

    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_messageWindowClassRegistered = true;
    return true;
}

void Application::ShowAboutWindow() {
    CreateOrActivateInfoWindow(
        InfoWindowKind::About,
        m_hAboutWindow,
        GetInfoWindowTitleByKind(static_cast<int>(InfoWindowKind::About)),
        BuildAboutText()
    );
}

void Application::ShowLogsWindow() {
    CreateOrActivateInfoWindow(
        InfoWindowKind::Logs,
        m_hLogsWindow,
        GetInfoWindowTitleByKind(static_cast<int>(InfoWindowKind::Logs)),
        BuildLogText()
    );
}

void Application::CreateOrActivateInfoWindow(InfoWindowKind kind, HWND& targetHandle, const wchar_t* title, const std::wstring& bodyText) {
    if (targetHandle && IsWindow(targetHandle)) {
        UpdateInfoWindowText(kind, bodyText);
        ShowWindow(targetHandle, SW_SHOWNORMAL);
        SetForegroundWindow(targetHandle);
        return;
    }

    InfoWindowState* state = new InfoWindowState();
    state->owner = this;
    state->kind = static_cast<int>(kind);
    state->title = title ? title : L"";
    state->text = bodyText;
    state->editBrush = CreateSolidBrush(RGB(45, 45, 45));

    RECT ownerRect = {};
    GetWindowRect(m_hWnd, &ownerRect);
    const bool isLogsWindow = kind == InfoWindowKind::Logs;
    const int width = isLogsWindow ? 700 : 560;
    const int height = isLogsWindow ? 480 : 360;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    const DWORD infoStyle = isLogsWindow
        ? WS_OVERLAPPEDWINDOW
        : (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX);

    HWND infoWindow = CreateWindowExW(
        0,
        INFO_WINDOW_CLASS_NAME,
        title,
        infoStyle,
        x, y, width, height,
        m_hWnd,
        nullptr,
        m_hInstance,
        state
    );

    if (!infoWindow) {
        if (state->editBrush) {
            DeleteObject(state->editBrush);
        }
        delete state;
        return;
    }

    targetHandle = infoWindow;
    ShowWindow(infoWindow, SW_SHOWNORMAL);
    UpdateWindow(infoWindow);
}

void Application::OnInfoWindowClosed(InfoWindowKind kind) {
    if (kind == InfoWindowKind::About) {
        m_hAboutWindow = nullptr;
    } else if (kind == InfoWindowKind::Logs) {
        m_hLogsWindow = nullptr;
    }
}

void Application::UpdateInfoWindowText(InfoWindowKind kind, const std::wstring& text) {
    HWND target = nullptr;
    if (kind == InfoWindowKind::About) {
        target = m_hAboutWindow;
    } else if (kind == InfoWindowKind::Logs) {
        target = m_hLogsWindow;
    }
    if (!target || !IsWindow(target)) {
        return;
    }

    auto* state = reinterpret_cast<InfoWindowState*>(GetWindowLongPtrW(target, GWLP_USERDATA));
    if (!state || !state->textControl) {
        return;
    }
    state->text = text;
    if (state->usesListBox) {
        FillListBoxWithText(state->textControl, state->text);
    } else {
        SetWindowTextW(state->textControl, state->text.c_str());
    }
}

int Application::ShowStyledMessageDialog(const wchar_t* title,
                                         const std::wstring& bodyText,
                                         const wchar_t* primaryButtonText,
                                         const wchar_t* secondaryButtonText) {
    if (!m_hWnd || !IsWindow(m_hWnd)) {
        return IDCANCEL;
    }

    int result = IDCANCEL;
    MessageWindowState* state = new MessageWindowState();
    state->owner = this;
    state->title = title ? title : L"Сообщение";
    state->text = bodyText;
    state->primaryButtonText = primaryButtonText ? primaryButtonText : L"OK";
    state->secondaryButtonText = secondaryButtonText ? secondaryButtonText : L"";
    state->hasSecondaryButton = secondaryButtonText != nullptr;
    state->useMonoFont = state->title.find(L"Ошибка") != std::wstring::npos
        || state->text.find(L"stderr:") != std::wstring::npos
        || state->text.find(L"stdout:") != std::wstring::npos;
    state->resultOut = &result;
    state->editBrush = CreateSolidBrush(RGB(45, 45, 45));

    RECT ownerRect = {};
    GetWindowRect(m_hWnd, &ownerRect);
    const int width = 500;
    const int height = 230;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND messageWindow = CreateWindowExW(
        0,
        MESSAGE_WINDOW_CLASS_NAME,
        title ? title : L"Сообщение",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        m_hWnd,
        nullptr,
        m_hInstance,
        state
    );

    if (!messageWindow) {
        if (state->editBrush) {
            DeleteObject(state->editBrush);
        }
        delete state;
        return IDCANCEL;
    }

    EnableWindow(m_hWnd, FALSE);
    ShowWindow(messageWindow, SW_SHOWNORMAL);
    UpdateWindow(messageWindow);

    MSG msg = {};
    while (IsWindow(messageWindow) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(messageWindow, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(messageWindow)) {
            break;
        }
    }

    EnableWindow(m_hWnd, TRUE);
    SetForegroundWindow(m_hWnd);
    return result;
}

void Application::ShowStyledMessage(const std::wstring& title, const std::wstring& message) {
    ShowStyledMessageDialog(title.c_str(), message, L"OK", nullptr);
}

void Application::CheckForUpdates() {
    if (!m_updateService) {
        m_updateService = std::make_unique<UpdateService>();
    }

    AppendLog(L"Проверка обновлений...");
    const UpdateCheckResult check = m_updateService->CheckForUpdates(APP_VERSION);
    if (!check.success) {
        AppendLog(L"[Update] Ошибка: " + check.errorMessage);
        ShowStyledMessage(L"Обновление", L"Ошибка проверки обновлений:\r\n" + check.errorMessage);
        return;
    }

    if (!check.updateAvailable) {
        AppendLog(L"[Update] Новая версия не найдена.");
        ShowStyledMessage(L"Обновление", L"У вас уже актуальная версия: " + std::wstring(APP_VERSION));
        return;
    }

    const std::wstring prompt = L"Доступна версия " + check.latestVersion + L" (" + check.latestTag + L").\r\nСкачать и установить?";
    const int decision = ShowStyledMessageDialog(L"Обновление", prompt, L"Обновить", L"Позже");
    if (decision != IDYES) {
        AppendLog(L"[Update] Обновление отложено.");
        return;
    }

    const std::wstring targetPath = GetExecutablePath();
    const std::wstring tmpPath = GetExecutableDirectory() + L"\\TextMagic.update.tmp.exe";

    std::wstring error;
    if (!m_updateService->DownloadReleaseExecutable(check.latestTag, tmpPath, error)) {
        AppendLog(L"[Update] Ошибка загрузки: " + error);
        ShowStyledMessage(L"Обновление", L"Не удалось загрузить обновление:\r\n" + error);
        return;
    }

    if (!m_updateService->LaunchUpdaterProcess(GetCurrentProcessId(), tmpPath, targetPath, error)) {
        AppendLog(L"[Update] Ошибка запуска: " + error);
        ShowStyledMessage(L"Обновление", L"Не удалось запустить обновление:\r\n" + error);
        return;
    }

    AppendLog(L"[Update] Обновление запущено.");
    ShowStyledMessage(L"Обновление", L"Обновление загружено. Приложение будет перезапущено.");
    PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
}

void Application::AppendLog(const std::wstring& line) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    wchar_t prefix[32] = {};
    swprintf_s(prefix, L"[%02u:%02u:%02u] ", st.wHour, st.wMinute, st.wSecond);
    m_executionLogs.push_back(std::wstring(prefix) + line);

    if (m_executionLogs.size() > 1000) {
        m_executionLogs.erase(m_executionLogs.begin(), m_executionLogs.begin() + 200);
    }

    UpdateInfoWindowText(InfoWindowKind::Logs, BuildLogText());
}

void Application::ClearLogs() {
    m_executionLogs.clear();
    UpdateInfoWindowText(InfoWindowKind::Logs, BuildLogText());
    SetStatusText(L(Localization::Key::StatusLogsCleared));
}

std::wstring Application::BuildAboutText() const {
    std::wostringstream stream;
    stream << L"TextMagic " << APP_VERSION << L"\r\n\r\n";
    stream << L(Localization::Key::AboutLoadedScriptsPrefix) << m_scripts.size() << L"\r\n";
    stream << L(Localization::Key::AboutScriptsDirectoryPrefix) << m_scriptsDirectory << L"\r\n\r\n";
    stream << L(Localization::Key::AboutCheckUpdatesHint);
    return stream.str();
}

std::wstring Application::BuildLogText() const {
    if (m_executionLogs.empty()) {
        return L(Localization::Key::LogIsEmpty);
    }

    std::wstring text;
    for (size_t i = 0; i < m_executionLogs.size(); ++i) {
        text += m_executionLogs[i];
        if (i + 1 < m_executionLogs.size()) {
            text += L"\r\n";
        }
    }
    return text;
}

LRESULT CALLBACK Application::InfoWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<InfoWindowState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<InfoWindowState*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_GETMINMAXINFO:
        if (state) {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const bool isLogs = state->kind == static_cast<int>(Application::InfoWindowKind::Logs);
            info->ptMinTrackSize.x = isLogs ? LOGS_MIN_WIDTH : INFO_MIN_WIDTH;
            info->ptMinTrackSize.y = isLogs ? LOGS_MIN_HEIGHT : INFO_MIN_HEIGHT;
            return 0;
        }
        break;

    case WM_CREATE:
        if (state) {
            state->titleLabel = CreateWindowExW(
                0, L"STATIC", state->title.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 100, 28,
                hWnd, nullptr, GetModuleHandleW(nullptr), nullptr
            );

            if (state->kind == static_cast<int>(Application::InfoWindowKind::Logs)) {
                state->usesListBox = true;
                state->textControl = CreateWindowExW(
                    0, L"LISTBOX", nullptr,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                    0, 0, 100, 100,
                    hWnd, reinterpret_cast<HMENU>(ID_INFO_TEXT), GetModuleHandleW(nullptr), nullptr
                );
                InitializeFlatSB(state->textControl);
                FlatSB_SetScrollProp(state->textControl, WSB_PROP_VSTYLE, FSB_FLAT_MODE, TRUE);
                FlatSB_SetScrollProp(state->textControl, WSB_PROP_VBKGCOLOR, RGB(30, 30, 30), TRUE);
                FillListBoxWithText(state->textControl, state->text);
            } else {
                state->textControl = CreateWindowExW(
                    0, L"EDIT", state->text.c_str(),
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY,
                    0, 0, 100, 100,
                    hWnd, reinterpret_cast<HMENU>(ID_INFO_TEXT), GetModuleHandleW(nullptr), nullptr
                );
            }
            if (state->textControl) {
                SetWindowSubclass(state->textControl, CopyOnlyContextSubclassProc, 1, reinterpret_cast<DWORD_PTR>(hWnd));
            }

            state->closeButton = CreateWindowExW(
                0, L"BUTTON", L(Localization::Key::InfoButtonClose),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 100, 34,
                hWnd, reinterpret_cast<HMENU>(ID_INFO_CLOSE), GetModuleHandleW(nullptr), nullptr
            );

            if (state->kind == static_cast<int>(Application::InfoWindowKind::About)) {
                state->actionButton = CreateWindowExW(
                    0, L"BUTTON", L(Localization::Key::InfoButtonCheckUpdates),
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 180, 34,
                    hWnd, reinterpret_cast<HMENU>(ID_INFO_ACTION), GetModuleHandleW(nullptr), nullptr
                );
            }

            HFONT textFont = state->usesListBox ? state->owner->m_hMonoFont : state->owner->m_hFont;
            SendMessageW(state->titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            SendMessageW(state->textControl, WM_SETFONT, reinterpret_cast<WPARAM>(textFont), TRUE);
            SendMessageW(state->closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            if (state->actionButton) {
                SendMessageW(state->actionButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            }
        }
        return 0;

    case WM_SIZE:
        if (state) {
            const int w = LOWORD(lParam);
            const int h = HIWORD(lParam);
            const int m = 16;
            const int titleH = 26;
            const int bh = 34;
            const int closeW = 130;
            const int actionW = 210;
            const int gap = 10;
            const int footerGap = 12;

            const int textTop = m + titleH + 6;
            const int textHeight = std::max(70, h - textTop - m - bh - footerGap);
            const int y = textTop + textHeight + footerGap;

            MoveWindow(state->titleLabel, m, m, w - 2 * m, titleH, TRUE);
            MoveWindow(state->textControl, m, textTop, w - 2 * m, textHeight, TRUE);
            MoveWindow(state->closeButton, w - m - closeW, y, closeW, bh, TRUE);
            if (state->actionButton) {
                MoveWindow(state->actionButton, w - m - closeW - gap - actionW, y, actionW, bh, TRUE);
            }
        }
        return 0;

    case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis && mis->CtlType == ODT_MENU && IsStyledMenuItem(ResolveStyledMenuItemId(mis->itemID, mis->itemData))) {
                MeasureStyledMenuItem(mis);
                return TRUE;
            }
        }
        break;

    case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!dis) {
                break;
            }
            if (dis->CtlType == ODT_MENU && IsStyledMenuItem(ResolveStyledMenuItemId(dis->itemID, dis->itemData))) {
                DrawStyledMenuItem(dis);
                return TRUE;
            }
            if (dis->CtlType == ODT_BUTTON) {
                wchar_t text[128] = {};
                GetWindowTextW(dis->hwndItem, text, 128);
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                const float hover = (dis->itemState & ODS_HOTLIGHT) ? 1.0f : 0.0f;
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, text, pressed, hover);
                return TRUE;
            }
        }
        break;

    case WM_INITMENUPOPUP:
        if (state && state->owner) {
            HMENU popupMenu = reinterpret_cast<HMENU>(wParam);
            if (popupMenu) {
                MENUINFO popupMenuInfo = {};
                popupMenuInfo.cbSize = sizeof(MENUINFO);
                popupMenuInfo.fMask = MIM_BACKGROUND;
                popupMenuInfo.hbrBack = state->owner->m_hCardBrush;
                SetMenuInfo(popupMenu, &popupMenuInfo);
            }
        }
        break;

    case WM_CONTEXTMENU:
        if (state) {
            HWND sourceControl = reinterpret_cast<HWND>(wParam);
            if (sourceControl == state->textControl) {
                state->contextMenuTarget = sourceControl;
                const POINT point = ResolveContextMenuPoint(sourceControl, lParam);
                ShowStyledContextMenu(hWnd, point, state->usesListBox, state->usesListBox);
                return 0;
            }
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT r = {};
            GetClientRect(hWnd, &r);
            UiRenderer::DrawBackground(hdc, r);
            RECT card = { 8, 8, r.right - 8, r.bottom - 8 };
            UiRenderer::DrawCard(hdc, card);
            EndPaint(hWnd, &ps);
            if (state && state->usesListBox) {
                UiRenderer::DrawEditBorder(hWnd, state->textControl);
            }
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        if (state) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            if (control == state->titleLabel) {
                SetTextColor(hdc, RGB(255, 255, 255));
                return reinterpret_cast<INT_PTR>(state->owner->m_hCardBrush);
            }
            SetTextColor(hdc, RGB(230, 230, 230));
            return reinterpret_cast<INT_PTR>(state->owner->m_hCardBrush);
        }
        break;

    case WM_CTLCOLOREDIT:
        if (state && state->editBrush) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(45, 45, 45));
            SetTextColor(hdc, RGB(245, 245, 245));
            return reinterpret_cast<INT_PTR>(state->editBrush);
        }
        break;

    case WM_CTLCOLORLISTBOX:
        if (state && state->usesListBox) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(37, 37, 37));
            SetTextColor(hdc, RGB(245, 245, 245));
            return reinterpret_cast<INT_PTR>(state->owner->m_hListBrush);
        }
        break;

    case WM_COMMAND:
        if (state) {
            const UINT id = LOWORD(wParam);
            if (id == ID_MENU_CONTEXT_COPY) {
                if (state->contextMenuTarget == state->textControl) {
                    if (state->usesListBox) {
                        std::wstring toCopy = GetSelectedListBoxText(state->textControl);
                        if (toCopy.empty()) {
                            toCopy = state->text;
                        }
                        CopyTextToClipboard(hWnd, toCopy);
                    } else {
                        CopyEditSelectionOrAll(state->textControl);
                    }
                }
                return 0;
            }
            if (id == ID_MENU_CONTEXT_SAVEAS) {
                if (state->usesListBox && state->contextMenuTarget == state->textControl) {
                    std::wstring savedPath;
                    std::wstring saveError;
                    if (SaveTextWithDialog(hWnd, state->text, &savedPath, &saveError)) {
                        if (state->owner) {
                            state->owner->AppendLog(L"[Logs] Сохранено в файл: " + savedPath);
                        }
                    } else if (!saveError.empty() && state->owner) {
                        state->owner->ShowStyledMessage(L"Ошибка сохранения", saveError);
                    }
                }
                return 0;
            }
            if (id == ID_MENU_CONTEXT_CLEAR_LOGS) {
                if (state->usesListBox && state->owner) {
                    state->owner->ClearLogs();
                }
                return 0;
            }
            if (id == ID_INFO_CLOSE || id == IDOK || id == IDCANCEL) {
                DestroyWindow(hWnd);
                return 0;
            }
            if (id == ID_INFO_ACTION && state->kind == static_cast<int>(Application::InfoWindowKind::About)) {
                state->owner->CheckForUpdates();
                return 0;
            }
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_NCDESTROY:
        if (state) {
            if (state->usesListBox && state->textControl && IsWindow(state->textControl)) {
                UninitializeFlatSB(state->textControl);
            }
            if (state->editBrush) {
                DeleteObject(state->editBrush);
            }
            if (state->owner) {
                state->owner->OnInfoWindowClosed(static_cast<Application::InfoWindowKind>(state->kind));
            }
            delete state;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK Application::MessageWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<MessageWindowState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<MessageWindowState*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_CREATE:
        if (state) {
            state->titleLabel = CreateWindowExW(
                0, L"STATIC", state->title.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 100, 24,
                hWnd, nullptr, GetModuleHandleW(nullptr), nullptr
            );
            state->textControl = CreateWindowExW(
                0, L"EDIT", state->text.c_str(),
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY,
                0, 0, 100, 100,
                hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_TEXT), GetModuleHandleW(nullptr), nullptr
            );
            if (state->textControl) {
                SetWindowSubclass(state->textControl, CopyOnlyContextSubclassProc, 1, reinterpret_cast<DWORD_PTR>(hWnd));
            }
            state->primaryButton = CreateWindowExW(
                0, L"BUTTON", state->primaryButtonText.c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 120, 34,
                hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_PRIMARY), GetModuleHandleW(nullptr), nullptr
            );
            if (state->hasSecondaryButton) {
                state->secondaryButton = CreateWindowExW(
                    0, L"BUTTON", state->secondaryButtonText.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 120, 34,
                    hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_SECONDARY), GetModuleHandleW(nullptr), nullptr
                );
            }
            SendMessageW(state->titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            HFONT textFont = state->useMonoFont ? state->owner->m_hMonoFont : state->owner->m_hFont;
            SendMessageW(state->textControl, WM_SETFONT, reinterpret_cast<WPARAM>(textFont), TRUE);
            SendMessageW(state->primaryButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            if (state->secondaryButton) {
                SendMessageW(state->secondaryButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            }
        }
        return 0;

    case WM_SIZE:
        if (state) {
            const int w = LOWORD(lParam);
            const int h = HIWORD(lParam);
            const int m = 14;
            const int titleH = 24;
            const int bh = 34;
            const int bw = 126;
            const int gap = 10;
            const int footerGap = 10;

            const int textTop = m + titleH + 6;
            const int textHeight = std::max(50, h - textTop - m - bh - footerGap);
            const int y = textTop + textHeight + footerGap;

            MoveWindow(state->titleLabel, m, m, w - 2 * m, titleH, TRUE);
            MoveWindow(state->textControl, m, textTop, w - 2 * m, textHeight, TRUE);
            if (state->hasSecondaryButton && state->secondaryButton) {
                const int px = w - m - bw;
                MoveWindow(state->primaryButton, px, y, bw, bh, TRUE);
                MoveWindow(state->secondaryButton, px - gap - bw, y, bw, bh, TRUE);
            } else {
                MoveWindow(state->primaryButton, w - m - bw, y, bw, bh, TRUE);
            }
        }
        return 0;

    case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis && mis->CtlType == ODT_MENU && IsStyledMenuItem(ResolveStyledMenuItemId(mis->itemID, mis->itemData))) {
                MeasureStyledMenuItem(mis);
                return TRUE;
            }
        }
        break;

    case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!dis) {
                break;
            }
            if (dis->CtlType == ODT_MENU && IsStyledMenuItem(ResolveStyledMenuItemId(dis->itemID, dis->itemData))) {
                DrawStyledMenuItem(dis);
                return TRUE;
            }
            if (dis->CtlType == ODT_BUTTON) {
                wchar_t text[128] = {};
                GetWindowTextW(dis->hwndItem, text, 128);
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                const float hover = (dis->itemState & ODS_HOTLIGHT) ? 1.0f : 0.0f;
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, text, pressed, hover);
                return TRUE;
            }
        }
        break;

    case WM_INITMENUPOPUP:
        if (state && state->owner) {
            HMENU popupMenu = reinterpret_cast<HMENU>(wParam);
            if (popupMenu) {
                MENUINFO popupMenuInfo = {};
                popupMenuInfo.cbSize = sizeof(MENUINFO);
                popupMenuInfo.fMask = MIM_BACKGROUND;
                popupMenuInfo.hbrBack = state->owner->m_hCardBrush;
                SetMenuInfo(popupMenu, &popupMenuInfo);
            }
        }
        break;

    case WM_CONTEXTMENU:
        if (state) {
            HWND sourceControl = reinterpret_cast<HWND>(wParam);
            if (sourceControl == state->textControl) {
                state->contextMenuTarget = sourceControl;
                const POINT point = ResolveContextMenuPoint(sourceControl, lParam);
                ShowStyledContextMenu(hWnd, point, false);
                return 0;
            }
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT r = {};
            GetClientRect(hWnd, &r);
            UiRenderer::DrawBackground(hdc, r);
            RECT card = { 8, 8, r.right - 8, r.bottom - 8 };
            UiRenderer::DrawCard(hdc, card);
            EndPaint(hWnd, &ps);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        if (state) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            if (control == state->titleLabel) {
                SetTextColor(hdc, RGB(255, 255, 255));
                return reinterpret_cast<INT_PTR>(state->owner->m_hCardBrush);
            }
            SetTextColor(hdc, RGB(230, 230, 230));
            return reinterpret_cast<INT_PTR>(state->owner->m_hCardBrush);
        }
        break;

    case WM_CTLCOLOREDIT:
        if (state && state->editBrush) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(45, 45, 45));
            SetTextColor(hdc, RGB(245, 245, 245));
            return reinterpret_cast<INT_PTR>(state->editBrush);
        }
        break;

    case WM_COMMAND:
        if (state) {
            const UINT id = LOWORD(wParam);
            if (id == ID_MENU_CONTEXT_COPY) {
                if (state->contextMenuTarget == state->textControl) {
                    CopyEditSelectionOrAll(state->textControl);
                }
                return 0;
            }
            if (id == ID_MESSAGE_PRIMARY || id == IDOK) {
                state->result = state->hasSecondaryButton ? IDYES : IDOK;
                DestroyWindow(hWnd);
                return 0;
            }
            if (id == ID_MESSAGE_SECONDARY || id == IDCANCEL) {
                state->result = state->hasSecondaryButton ? IDNO : IDCANCEL;
                DestroyWindow(hWnd);
                return 0;
            }
        }
        break;

    case WM_CLOSE:
        if (state) {
            state->result = state->hasSecondaryButton ? IDNO : IDCANCEL;
        }
        DestroyWindow(hWnd);
        return 0;

    case WM_NCDESTROY:
        if (state) {
            if (state->editBrush) {
                DeleteObject(state->editBrush);
            }
            if (state->resultOut) {
                *state->resultOut = state->result;
            }
            delete state;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}
