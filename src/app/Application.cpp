#include "Application.h"

#include "AppUiHelpers.h"
#include "ClipboardUtils.h"
#include "EncodingUtils.h"
#include "FullscreenUtils.h"
#include "Localization.h"
#include "ScriptInputSource.h"
#include "TextBridgeInputUtils.h"
#include "ToolTip.h"
#include "UiRenderer.h"
#include "InputBuffer.h"
#include "LogFile.h"
#include "PowerShellUtils.h"
#include "RunningApplication.h"
#include "resource.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <objbase.h>
#include <richedit.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace {
const wchar_t* WINDOW_CLASS_NAME = TM_APP_NAME_W L"WinApiClass";
const wchar_t* INFO_WINDOW_CLASS_NAME = TM_APP_NAME_W L"InfoWindowClass";
const wchar_t* MESSAGE_WINDOW_CLASS_NAME = TM_APP_NAME_W L"MessageWindowClass";
const wchar_t* MORE_POPUP_WINDOW_CLASS_NAME = TM_APP_NAME_W L"MorePopupWindowClass";
const wchar_t* SINGLE_INSTANCE_MUTEX_NAME = L"Local\\" TM_APP_NAME_W L".SingleInstance";

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
    ID_MENU_TRAY_EXIT = 2015,
    ID_MENU_LANGUAGE_LABEL = 2016,
    ID_MENU_INPUT_MODE_PREVIOUS_WORD = 2017,
    ID_MENU_INPUT_MODE_ALL_TEXT = 2018,
    ID_MENU_INPUT_MODE_LABEL = 2019,
    ID_MENU_APPLICATION_BLACKLIST = 2020
};

constexpr UINT ID_MENU_LANGUAGE_DYNAMIC_FIRST = 2300;
constexpr UINT ID_MENU_LANGUAGE_DYNAMIC_LAST = 2399;

enum InfoControlId {
    ID_INFO_TEXT = 2101,
    ID_INFO_CLOSE = 2102,
    ID_INFO_ACTION = 2103,
    ID_INFO_FULLSCREEN_CHECKBOX = 2104,
    ID_INFO_BLACKLIST_LIST = 2105,
    ID_INFO_RUNNING_PICKER = 2106,
    ID_INFO_EXE_PICKER = 2107,
    ID_INFO_REMOVE_BLACKLIST = 2108
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
    HWND fullscreenCheckbox = nullptr;
    HWND blacklistList = nullptr;
    HWND runningPickerButton = nullptr;
    HWND exePickerButton = nullptr;
    HWND removeButton = nullptr;
    HWND contextMenuTarget = nullptr;
    std::wstring title;
    std::wstring text;
    HBRUSH editBrush = nullptr;
    bool richEdit = false;
    bool logPlaceholderVisible = false;
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
    bool usesListBox = false;
    bool runningApplicationSelection = false;
    int runningSortColumn = -1;
    bool runningSortAscending = true;
    std::vector<RunningApplication> runningApplications;
    std::vector<std::wstring>* selectedApplicationPathsOut = nullptr;
    int result = IDCANCEL;
    int* resultOut = nullptr;
    HBRUSH editBrush = nullptr;
};

int CALLBACK CompareRunningApplicationRows(
    LPARAM leftRow,
    LPARAM rightRow,
    LPARAM context
) {
    auto* state = reinterpret_cast<MessageWindowState*>(context);
    LVITEMW leftItem = {};
    leftItem.mask = LVIF_PARAM;
    leftItem.iItem = static_cast<int>(leftRow);
    LVITEMW rightItem = {};
    rightItem.mask = LVIF_PARAM;
    rightItem.iItem = static_cast<int>(rightRow);
    ListView_GetItem(state->textControl, &leftItem);
    ListView_GetItem(state->textControl, &rightItem);
    const auto& left = state->runningApplications[static_cast<size_t>(leftItem.lParam)];
    const auto& right = state->runningApplications[static_cast<size_t>(rightItem.lParam)];
    const int result = CompareRunningApplications(
        left,
        right,
        static_cast<RunningApplicationColumn>(state->runningSortColumn)
    );
    return state->runningSortAscending ? result : -result;
}

void SetRunningApplicationSortIndicator(HWND listView, int column, bool ascending) {
    HWND header = ListView_GetHeader(listView);
    const int count = Header_GetItemCount(header);
    for (int index = 0; index < count; ++index) {
        HDITEMW item = {};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, index, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (index == column) {
            item.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(header, index, &item);
    }
    InvalidateRect(header, nullptr, TRUE);
}

constexpr int HOTKEY_BASE = 5000;
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_SCRIPT_EXECUTION_COMPLETE = WM_APP + 2;
constexpr LRESULT SCRIPT_EXECUTION_COMPLETE_HANDLED = 1;
constexpr UINT WM_UPDATE_CHECK_COMPLETE = WM_APP + 3;
constexpr UINT WM_UPDATE_INSTALL_COMPLETE = WM_APP + 4;
constexpr UINT WM_IMPORT_ZIP_COMPLETE = WM_APP + 5;
constexpr UINT WM_EXPORT_ZIP_COMPLETE = WM_APP + 6;
constexpr UINT MORE_POPUP_TRACK_TIMER_ID = 0x4D31;
constexpr UINT TRAY_ICON_ID = 1;
constexpr int LOGS_MIN_WIDTH = 640;
constexpr int LOGS_MIN_HEIGHT = 420;
constexpr int INFO_MIN_WIDTH = 500;
constexpr int INFO_MIN_HEIGHT = 300;
constexpr int LIST_CONTENT_PADDING = 6;
constexpr int LIST_ITEM_HEIGHT = 24;
constexpr int LIST_TEXT_PADDING = 9;
constexpr UINT_PTR DARK_HEADER_SUBCLASS_ID = 1;
constexpr const wchar_t* LOG_FILE_NAME = TM_APP_NAME_W L".log";
constexpr const wchar_t* LANGUAGE_SETTINGS_FILE_NAME = TM_APP_NAME_W L".settings.ini";
constexpr const wchar_t* LANGUAGE_SETTINGS_SECTION = L"ui";
constexpr const wchar_t* LANGUAGE_SETTINGS_KEY = L"language";
constexpr const wchar_t* SCRIPT_INPUT_SETTINGS_KEY = L"script_input_mode";
constexpr const wchar_t* DISABLE_FULLSCREEN_HOTKEYS_SETTINGS_KEY =
    L"disable_hotkeys_in_fullscreen";
constexpr const wchar_t* SCRIPT_INPUT_MODE_PREVIOUS_WORD = L"previous_word";
constexpr const wchar_t* SCRIPT_INPUT_MODE_ALL_TEXT = L"all_text";
constexpr int MORE_POPUP_ITEM_HEIGHT = 34;
constexpr int MORE_POPUP_SEPARATOR_HEIGHT = 10;
constexpr int MORE_POPUP_MIN_WIDTH = 170;
constexpr int MORE_POPUP_ARROW_EXTRA_WIDTH = 48;
constexpr int MORE_POPUP_ITEM_EXTRA_WIDTH = 34;
constexpr int MORE_POPUP_WIDTH_PADDING = 14;
constexpr int MORE_POPUP_TRACK_INTERVAL_MS = 25;
constexpr ULONGLONG HOTKEY_DOUBLE_TAP_TIMEOUT_MS = 350;

struct CheckboxVisualState {
    bool hot = false;
};

void PaintDarkListViewHeader(HWND header, HDC hdc);

LRESULT CALLBACK DarkHeaderSubclassProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData
);

void ApplyDarkListViewHeader(HWND listView);

bool DrawPaddedListBoxItem(const DRAWITEMSTRUCT* item) {
    if (!item || item->CtlType != ODT_LISTBOX || item->itemID == static_cast<UINT>(-1)) {
        return false;
    }

    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    SetDCBrushColor(item->hDC, selected ? RGB(58, 58, 58) : RGB(37, 37, 37));
    FillRect(item->hDC, &item->rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    const LRESULT length = SendMessageW(item->hwndItem, LB_GETTEXTLEN, item->itemID, 0);
    if (length >= 0) {
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, reinterpret_cast<LPARAM>(text.data()));
        text.resize(static_cast<size_t>(length));

        RECT textRect = item->rcItem;
        textRect.left += LIST_TEXT_PADDING;
        textRect.right -= LIST_TEXT_PADDING;
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(245, 245, 245));
        DrawTextW(item->hDC, text.c_str(), static_cast<int>(text.size()), &textRect,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if ((item->itemState & ODS_FOCUS) != 0) {
        DrawFocusRect(item->hDC, &item->rcItem);
    }
    return true;
}

struct ScriptExecutionTaskResult {
    std::wstring scriptName;
    std::wstring sourceText;
    InputBuffer::PreviousWordCapture inputCapture;
    HWND inputTargetWindow = nullptr;
    bool hasSelection = false;
    bool inputBufferMode = false;
    bool allTextInputMode = false;
    bool clipboardMode = false;
    bool noTextAvailable = false;
    bool executeOk = false;
    bool inputReady = true;
    std::wstring outputText;
    std::wstring executionError;
};

struct UpdateCheckTaskResult {
    UpdateCheckResult check;
};

struct UpdateInstallTaskResult {
    bool success = false;
    std::wstring error;
};

struct ImportZipTaskResult {
    bool success = false;
    std::wstring errorMessage;
    std::vector<std::wstring> importedPaths;
    int importedCount = 0;
    int skippedCount = 0;
};

struct ExportZipTaskResult {
    bool success = false;
    std::wstring errorMessage;
    std::wstring archivePath;
    int scriptFileCount = 0;
};

void SendScriptExecutionCompletion(
    HWND windowHandle,
    std::unique_ptr<ScriptExecutionTaskResult> result
) {
    DWORD processId = 0;
    GetWindowThreadProcessId(windowHandle, &processId);
    if (processId != GetCurrentProcessId()) {
        return;
    }

    ScriptExecutionTaskResult* payload = result.release();
    if (SendMessageW(
            windowHandle,
            WM_SCRIPT_EXECUTION_COMPLETE,
            reinterpret_cast<WPARAM>(payload),
            0) != SCRIPT_EXECUTION_COMPLETE_HANDLED) {
        delete payload;
    }
}

const wchar_t* T(const wchar_t* key) {
    return Localization::GetTextByName(key);
}

template <typename TResult>
void PostOwnedMessage(HWND windowHandle, UINT message, TResult* result) {
    if (!result) {
        return;
    }
    if (!PostMessageW(windowHandle, message, reinterpret_cast<WPARAM>(result), 0)) {
        delete result;
    }
}

std::map<UINT, std::wstring> g_languageMenuTextById;
std::map<UINT, std::wstring> g_languageMenuCodeById;
std::mutex g_inputBufferMutex;
InputBuffer g_inputBuffer;
std::mutex g_registeredHotkeysMutex;
struct TrackedHotkey {
    UINT modifiers = 0;
    UINT virtualKey = 0;
};
std::vector<TrackedHotkey> g_trackedHotkeys;
std::mutex g_hookHotkeysMutex;
struct HookHotkey {
    int hotkeyId = 0;
    UINT modifiers = 0;
    UINT virtualKey = 0;
    bool armed = true;
    ULONGLONG pendingTapTick = 0;
    DWORD pendingTapVkCode = 0;
};
std::vector<HookHotkey> g_hookHotkeys;
HotkeyDispatch::PressedKeyState g_hookKeyState;
HWND g_hotkeyDispatchWindow = nullptr;
ApplicationBlacklist* g_applicationBlacklist = nullptr;
ScriptExecutionGate* g_scriptExecutionGate = nullptr;
bool g_disableHotkeysInFullscreen = false;
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;
constexpr UINT HOTKEY_MODIFIER_MASK = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

struct ForegroundBlockCache {
    HWND window = nullptr;
    std::uint64_t blacklistGeneration = 0;
    bool blocked = false;
};

ForegroundBlockCache& GetForegroundBlockCache() {
    static ForegroundBlockCache cache;
    return cache;
}

InputBuffer::ContextId CurrentInputContext() {
    return reinterpret_cast<InputBuffer::ContextId>(GetForegroundWindow());
}

void ClearInputBuffer() {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    g_inputBuffer.Clear();
}

void PopInputBufferCharacter(InputBuffer::ContextId contextId) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    g_inputBuffer.PopCharacter(contextId);
}

void AppendInputBufferText(InputBuffer::ContextId contextId, const std::wstring& text) {
    if (text.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    g_inputBuffer.AppendText(contextId, text);
}

void ClearTrackedHotkeys() {
    std::lock_guard<std::mutex> lock(g_registeredHotkeysMutex);
    g_trackedHotkeys.clear();
}

void AddTrackedHotkey(UINT modifiers, UINT virtualKey) {
    std::lock_guard<std::mutex> lock(g_registeredHotkeysMutex);
    g_trackedHotkeys.push_back({ modifiers & HOTKEY_MODIFIER_MASK, virtualKey });
}

void ClearHookHotkeys() {
    std::lock_guard<std::mutex> lock(g_hookHotkeysMutex);
    g_hookHotkeys.clear();
}

void SetHotkeyDispatchWindow(HWND window) {
    std::lock_guard<std::mutex> lock(g_hookHotkeysMutex);
    g_hotkeyDispatchWindow = window;
}

void AddHookHotkey(int hotkeyId, UINT modifiers, UINT virtualKey) {
    std::lock_guard<std::mutex> lock(g_hookHotkeysMutex);
    g_hookHotkeys.push_back({ hotkeyId, modifiers & HOTKEY_MODIFIER_MASK, virtualKey, true });
}

bool TryGetWindowExecutablePath(HWND window, std::wstring* path) {
    if (!window || !path) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) {
        return false;
    }

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return false;
    }

    std::wstring executablePath(32768, L'\0');
    DWORD executablePathLength = static_cast<DWORD>(executablePath.size());
    const bool queried = QueryFullProcessImageNameW(
        process,
        0,
        executablePath.data(),
        &executablePathLength
    ) != FALSE;
    CloseHandle(process);
    if (!queried) {
        return false;
    }

    executablePath.resize(executablePathLength);
    *path = std::move(executablePath);
    return true;
}

BOOL CALLBACK CollectVisibleRunningApplication(HWND window, LPARAM parameter) {
    if (!IsWindowVisible(window)) {
        return TRUE;
    }

    const int titleLength = GetWindowTextLengthW(window);
    if (titleLength <= 0) {
        return TRUE;
    }

    std::wstring title(static_cast<size_t>(titleLength) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), titleLength + 1);
    if (copied <= 0) {
        return TRUE;
    }
    title.resize(static_cast<size_t>(copied));

    std::wstring path;
    if (!TryGetWindowExecutablePath(window, &path)) {
        return TRUE;
    }

    auto* applications = reinterpret_cast<std::vector<RunningApplication>*>(parameter);
    applications->push_back({ fs::path(path).filename().wstring(), std::move(title), std::move(path) });
    return TRUE;
}

std::vector<RunningApplication> EnumerateVisibleRunningApplications() {
    std::vector<RunningApplication> applications;
    EnumWindows(CollectVisibleRunningApplication, reinterpret_cast<LPARAM>(&applications));
    return DeduplicateRunningApplications(applications);
}

bool IsForegroundHandlingBlocked() {
    if (g_disableHotkeysInFullscreen
        && FullscreenUtils::IsForegroundWindowFullscreen()) {
        return true;
    }
    if (!g_applicationBlacklist) {
        return false;
    }

    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow) {
        return false;
    }

    ForegroundBlockCache& cache = GetForegroundBlockCache();
    const std::uint64_t generation = g_applicationBlacklist->Generation();
    if (cache.window == foregroundWindow
        && cache.blacklistGeneration == generation) {
        return cache.blocked;
    }

    std::wstring executablePath;
    cache.window = foregroundWindow;
    cache.blacklistGeneration = generation;
    cache.blocked = false;
    if (!TryGetWindowExecutablePath(foregroundWindow, &executablePath)) {
        return cache.blocked;
    }

    cache.blocked = g_applicationBlacklist->Contains(executablePath);
    return cache.blocked;
}

void InvalidateForegroundBlockCache() {
    GetForegroundBlockCache() = {};
}

bool IsDuplicateModifierHotkey(UINT modifiers, UINT virtualKey) {
    return (virtualKey == VK_SHIFT && (modifiers & MOD_SHIFT) != 0)
        || (virtualKey == VK_CONTROL && (modifiers & MOD_CONTROL) != 0)
        || (virtualKey == VK_MENU && (modifiers & MOD_ALT) != 0)
        || ((virtualKey == VK_LWIN || virtualKey == VK_RWIN) && (modifiers & MOD_WIN) != 0);
}

UINT GetModifierMaskForVirtualKey(UINT virtualKey) {
    if (virtualKey == VK_SHIFT) {
        return MOD_SHIFT;
    }
    if (virtualKey == VK_CONTROL) {
        return MOD_CONTROL;
    }
    if (virtualKey == VK_MENU) {
        return MOD_ALT;
    }
    if (virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
        return MOD_WIN;
    }
    return 0;
}

bool IsSameModifierTapKey(DWORD firstVkCode, DWORD secondVkCode, UINT hotkeyVirtualKey) {
    if (firstVkCode == secondVkCode) {
        return true;
    }
    if (hotkeyVirtualKey == VK_SHIFT) {
        return (firstVkCode == VK_SHIFT && (secondVkCode == VK_LSHIFT || secondVkCode == VK_RSHIFT))
            || (secondVkCode == VK_SHIFT && (firstVkCode == VK_LSHIFT || firstVkCode == VK_RSHIFT));
    }
    if (hotkeyVirtualKey == VK_CONTROL) {
        return (firstVkCode == VK_CONTROL && (secondVkCode == VK_LCONTROL || secondVkCode == VK_RCONTROL))
            || (secondVkCode == VK_CONTROL && (firstVkCode == VK_LCONTROL || firstVkCode == VK_RCONTROL));
    }
    if (hotkeyVirtualKey == VK_MENU) {
        return (firstVkCode == VK_MENU && (secondVkCode == VK_LMENU || secondVkCode == VK_RMENU))
            || (secondVkCode == VK_MENU && (firstVkCode == VK_LMENU || firstVkCode == VK_RMENU));
    }
    return false;
}

void ResetPendingModifierTap(HookHotkey* hotkey) {
    if (!hotkey) {
        return;
    }
    hotkey->pendingTapTick = 0;
    hotkey->pendingTapVkCode = 0;
}

bool IsDualModifierPressed(UINT hotkeyVirtualKey, DWORD inputVkCode) {
    if (hotkeyVirtualKey == VK_SHIFT) {
        if (inputVkCode == VK_LSHIFT) {
            return g_hookKeyState.IsPressed(VK_RSHIFT);
        }
        if (inputVkCode == VK_RSHIFT) {
            return g_hookKeyState.IsPressed(VK_LSHIFT);
        }
        return g_hookKeyState.IsPressed(VK_LSHIFT) && g_hookKeyState.IsPressed(VK_RSHIFT);
    }
    if (hotkeyVirtualKey == VK_CONTROL) {
        if (inputVkCode == VK_LCONTROL) {
            return g_hookKeyState.IsPressed(VK_RCONTROL);
        }
        if (inputVkCode == VK_RCONTROL) {
            return g_hookKeyState.IsPressed(VK_LCONTROL);
        }
        return g_hookKeyState.IsPressed(VK_LCONTROL) && g_hookKeyState.IsPressed(VK_RCONTROL);
    }
    if (hotkeyVirtualKey == VK_MENU) {
        if (inputVkCode == VK_LMENU) {
            return g_hookKeyState.IsPressed(VK_RMENU);
        }
        if (inputVkCode == VK_RMENU) {
            return g_hookKeyState.IsPressed(VK_LMENU);
        }
        return g_hookKeyState.IsPressed(VK_LMENU) && g_hookKeyState.IsPressed(VK_RMENU);
    }
    if (hotkeyVirtualKey == VK_LWIN || hotkeyVirtualKey == VK_RWIN) {
        if (inputVkCode == VK_LWIN) {
            return g_hookKeyState.IsPressed(VK_RWIN);
        }
        if (inputVkCode == VK_RWIN) {
            return g_hookKeyState.IsPressed(VK_LWIN);
        }
        return g_hookKeyState.IsPressed(VK_LWIN) && g_hookKeyState.IsPressed(VK_RWIN);
    }
    return true;
}

bool IsHotkeyMatchedByKeyEvent(UINT modifiers, UINT virtualKey, DWORD inputVkCode, UINT currentModifiers) {
    if ((modifiers & HOTKEY_MODIFIER_MASK) != currentModifiers) {
        return false;
    }
    if (!HotkeyDispatch::MatchesVirtualKey(inputVkCode, virtualKey)) {
        return false;
    }
    if (IsDuplicateModifierHotkey(modifiers, virtualKey)) {
        return IsDualModifierPressed(virtualKey, inputVkCode);
    }
    return true;
}

HotkeyDispatch::Action DispatchHookHotkeysOnKeyDown(DWORD inputVkCode, UINT currentModifiers) {
    const ULONGLONG nowTick = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_hookHotkeysMutex);
    if (!g_hotkeyDispatchWindow || !IsWindow(g_hotkeyDispatchWindow)) {
        return HotkeyDispatch::Action::PassThrough;
    }
    for (const auto& hotkey : g_hookHotkeys) {
        if (HotkeyDispatch::ShouldConsumeHeldRepeat(
                hotkey.armed,
                inputVkCode,
                hotkey.virtualKey)) {
            return HotkeyDispatch::DecideHeldRepeat(true, false, false);
        }
    }
    for (auto& hotkey : g_hookHotkeys) {
        if (!hotkey.armed) {
            continue;
        }
        if (IsDuplicateModifierHotkey(hotkey.modifiers, hotkey.virtualKey)) {
            if (hotkey.pendingTapTick != 0 && nowTick - hotkey.pendingTapTick > HOTKEY_DOUBLE_TAP_TIMEOUT_MS) {
                ResetPendingModifierTap(&hotkey);
            }
            const UINT effectiveModifiers = currentModifiers | GetModifierMaskForVirtualKey(hotkey.virtualKey);
            if ((hotkey.modifiers & HOTKEY_MODIFIER_MASK) != effectiveModifiers
                || !HotkeyDispatch::MatchesVirtualKey(inputVkCode, hotkey.virtualKey)) {
                ResetPendingModifierTap(&hotkey);
                continue;
            }

            const bool dualPressed = IsDualModifierPressed(hotkey.virtualKey, inputVkCode);
            const bool sameKeyTappedTwice = hotkey.pendingTapTick != 0
                && IsSameModifierTapKey(hotkey.pendingTapVkCode, inputVkCode, hotkey.virtualKey)
                && nowTick - hotkey.pendingTapTick <= HOTKEY_DOUBLE_TAP_TIMEOUT_MS;
            if (!dualPressed && !sameKeyTappedTwice) {
                hotkey.pendingTapTick = nowTick;
                hotkey.pendingTapVkCode = inputVkCode;
                continue;
            }
            ResetPendingModifierTap(&hotkey);
        } else if (!IsHotkeyMatchedByKeyEvent(hotkey.modifiers, hotkey.virtualKey, inputVkCode, currentModifiers)) {
            continue;
        }
        const HotkeyDispatch::Action consumeAction = HotkeyDispatch::BeginMatchedPress(hotkey.armed);
        if (!g_scriptExecutionGate || !g_scriptExecutionGate->TryReserve(nowTick)) {
            return consumeAction;
        }
        if (!PostMessageW(g_hotkeyDispatchWindow, WM_HOTKEY, static_cast<WPARAM>(hotkey.hotkeyId), 0)) {
            g_scriptExecutionGate->Release(nowTick);
            return consumeAction;
        }
        return HotkeyDispatch::Action::Dispatch;
    }
    return HotkeyDispatch::Action::PassThrough;
}

void RearmHookHotkeysIfReleased(DWORD releasedVkCode) {
    std::lock_guard<std::mutex> lock(g_hookHotkeysMutex);
    for (auto& hotkey : g_hookHotkeys) {
        HotkeyDispatch::RearmOnReleasedKey(hotkey.armed, hotkey.virtualKey, releasedVkCode);
    }
}

bool IsTrackedHotkeyPressed(DWORD vkCode, UINT currentModifiers) {
    std::lock_guard<std::mutex> lock(g_registeredHotkeysMutex);
    for (const TrackedHotkey& hotkey : g_trackedHotkeys) {
        if (IsHotkeyMatchedByKeyEvent(hotkey.modifiers, hotkey.virtualKey, vkCode, currentModifiers)) {
            return true;
        }
    }
    return false;
}

void AppendKeyToInputBuffer(DWORD vkCode, DWORD scanCode) {
    BYTE keyboardState[256] = {};
    if (!GetKeyboardState(keyboardState)) {
        return;
    }

    if (vkCode < 256) {
        keyboardState[vkCode] |= 0x80;
    }

    HKL keyboardLayout = GetKeyboardLayout(0);
    const HWND foreground = GetForegroundWindow();
    if (foreground) {
        const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
        if (threadId != 0) {
            keyboardLayout = GetKeyboardLayout(threadId);
        }
    }

    wchar_t text[8] = {};
    const int converted = ToUnicodeEx(
        static_cast<UINT>(vkCode),
        static_cast<UINT>(scanCode),
        keyboardState,
        text,
        static_cast<int>(_countof(text) - 1),
        0,
        keyboardLayout
    );

    if (converted > 0) {
        AppendInputBufferText(CurrentInputContext(), std::wstring(text, text + converted));
    } else if (converted < 0) {
        ToUnicodeEx(
            static_cast<UINT>(vkCode),
            static_cast<UINT>(scanCode),
            keyboardState,
            text,
            static_cast<int>(_countof(text) - 1),
            0,
            keyboardLayout
        );
    }
}

void HandleInputBufferKeyDown(DWORD vkCode, DWORD scanCode, UINT currentModifiers) {
    const bool controlDown = (currentModifiers & MOD_CONTROL) != 0;
    const bool altDown = (currentModifiers & MOD_ALT) != 0;
    const bool winDown = (currentModifiers & MOD_WIN) != 0;

    if (IsTrackedHotkeyPressed(vkCode, currentModifiers)) {
        return;
    }

    switch (vkCode) {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_INSERT:
    case VK_DELETE:
    case VK_RETURN:
    case VK_TAB:
    case VK_ESCAPE:
        ClearInputBuffer();
        return;
    case VK_BACK:
        if (controlDown || altDown || winDown) {
            ClearInputBuffer();
        } else {
            PopInputBufferCharacter(CurrentInputContext());
        }
        return;
    default:
        break;
    }

    if (HotkeyDispatch::IsModifierVirtualKey(vkCode)) {
        return;
    }
    if (vkCode >= VK_F1 && vkCode <= VK_F24) {
        ClearInputBuffer();
        return;
    }

    if (controlDown || altDown || winDown) {
        ClearInputBuffer();
        return;
    }

    AppendKeyToInputBuffer(vkCode, scanCode);
}

LRESULT CALLBACK InputKeyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* keyInfo = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (keyInfo && (keyInfo->flags & LLKHF_INJECTED) == 0) {
            const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
            if (keyDown || keyUp) {
                g_hookKeyState.Update(
                    HotkeyDispatch::NormalizeHookVirtualKey(
                        keyInfo->vkCode,
                        keyInfo->scanCode,
                        keyInfo->flags),
                    keyDown);
            }
            if ((keyDown || keyUp) && IsForegroundHandlingBlocked()) {
                if (HotkeyDispatch::ShouldRearmBlockedKeyEvent(keyUp)) {
                    RearmHookHotkeysIfReleased(keyInfo->vkCode);
                }
                return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
            }
            if (keyDown) {
                const UINT currentModifiers = g_hookKeyState.Modifiers();
                const HotkeyDispatch::Action action = DispatchHookHotkeysOnKeyDown(
                    keyInfo->vkCode,
                    currentModifiers);
                if (action != HotkeyDispatch::Action::PassThrough) {
                    return 1;
                }
                HandleInputBufferKeyDown(keyInfo->vkCode, keyInfo->scanCode, currentModifiers);
            } else if (keyUp) {
                RearmHookHotkeysIfReleased(keyInfo->vkCode);
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

LRESULT CALLBACK InputMouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION
        && (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN
            || wParam == WM_MBUTTONDOWN || wParam == WM_XBUTTONDOWN)) {
        const auto* mouseInfo = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (!mouseInfo || (mouseInfo->flags & LLMHF_INJECTED) == 0) {
            ClearInputBuffer();
        }
    }
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

bool InstallInputHooks(HINSTANCE hInstance) {
    if (!g_keyboardHook) {
        g_hookKeyState.Clear();
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, InputKeyboardHookProc, hInstance, 0);
    }
    if (!g_keyboardHook) {
        return false;
    }
    if (!g_mouseHook) {
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, InputMouseHookProc, hInstance, 0);
    }
    return true;
}

void UninstallInputHooks() {
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
        g_hookKeyState.Clear();
    }
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

bool PeekPreviousWordFromInputBuffer(
    InputBuffer::ContextId contextId,
    InputBuffer::PreviousWordCapture* capture
) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    return g_inputBuffer.TryPeekPreviousWord(contextId, capture);
}

bool PeekAllTextFromInputBuffer(
    InputBuffer::ContextId contextId,
    InputBuffer::PreviousWordCapture* capture
) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    return g_inputBuffer.TryPeekAllText(contextId, capture);
}

bool IsPreviousWordCaptureCurrent(const InputBuffer::PreviousWordCapture& capture) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    return CurrentInputContext() == capture.contextId
        && g_inputBuffer.IsCaptureCurrent(capture.contextId, capture);
}

bool CommitPreviousWordReplacement(const InputBuffer::PreviousWordCapture& capture, const std::wstring& replacement) {
    std::lock_guard<std::mutex> lock(g_inputBufferMutex);
    return g_inputBuffer.CommitReplacement(capture.contextId, capture, replacement);
}

bool ImportScriptFileToDirectory(const std::wstring& scriptsDirectory,
                                 const std::wstring& sourcePath,
                                 std::wstring* copiedPath) {
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
    fs::create_directories(fs::path(scriptsDirectory), createDirError);
    if (createDirError) {
        return false;
    }

    fs::path destination = fs::path(scriptsDirectory) / sourceFile.filename();
    std::error_code equivalentError;
    if (fs::exists(destination) && fs::equivalent(sourceFile, destination, equivalentError) && !equivalentError) {
        return false;
    }

    if (fs::exists(destination)) {
        const std::wstring stem = destination.stem().wstring();
        const std::wstring ext = destination.extension().wstring();
        int suffix = 1;
        while (fs::exists(destination)) {
            destination = fs::path(scriptsDirectory) / (stem + L"_" + std::to_wstring(suffix) + ext);
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

bool IsDynamicLanguageMenuId(UINT itemId) {
    return itemId >= ID_MENU_LANGUAGE_DYNAMIC_FIRST && itemId <= ID_MENU_LANGUAGE_DYNAMIC_LAST;
}

void ClearDynamicLanguageMenuItems() {
    g_languageMenuTextById.clear();
    g_languageMenuCodeById.clear();
}

bool TryGetLanguageCodeByMenuId(UINT itemId, std::wstring* languageCode) {
    if (languageCode) {
        languageCode->clear();
    }
    const auto it = g_languageMenuCodeById.find(itemId);
    if (it == g_languageMenuCodeById.end()) {
        return false;
    }
    if (languageCode) {
        *languageCode = it->second;
    }
    return true;
}

UINT ResolveStyledMenuItemId(UINT itemId, ULONG_PTR itemData);

std::wstring GetLanguageSettingsPath(const std::wstring& executableDirectory) {
    return executableDirectory + L"\\" + LANGUAGE_SETTINGS_FILE_NAME;
}

std::wstring ParseLanguageCodeSetting(const wchar_t* value) {
    if (!value || value[0] == L'\0') {
        return L"ru";
    }
    std::wstring code = value;
    std::transform(code.begin(), code.end(), code.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return code;
}

bool ParseScriptInputAllTextSetting(const wchar_t* value) {
    if (!value || value[0] == L'\0') {
        return false;
    }
    std::wstring mode = value;
    std::transform(mode.begin(), mode.end(), mode.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return mode == SCRIPT_INPUT_MODE_ALL_TEXT;
}

using fnOpenNcThemeData = HTHEME(WINAPI *)(HWND hWnd, LPCWSTR classList);
using fnAllowDarkModeForWindow = bool (WINAPI *)(HWND hWnd, bool allow);
using fnAllowDarkModeForApp = bool (WINAPI *)(bool allow);
using fnRefreshImmersiveColorPolicyState = void (WINAPI *)();

enum PreferredAppMode {
    AppModeDefault,
    AppModeAllowDark,
    AppModeForceDark,
    AppModeForceLight,
    AppModeMax
};
using fnSetPreferredAppMode = PreferredAppMode (WINAPI *)(PreferredAppMode appMode);

fnOpenNcThemeData g_openNcThemeData = nullptr;
fnAllowDarkModeForWindow g_allowDarkModeForWindow = nullptr;
fnAllowDarkModeForApp g_allowDarkModeForApp = nullptr;
fnRefreshImmersiveColorPolicyState g_refreshImmersiveColorPolicyState = nullptr;
fnSetPreferredAppMode g_setPreferredAppMode = nullptr;

template <typename T, typename T1, typename T2>
constexpr T RvaToVa(T1 base, T2 rva) {
    return reinterpret_cast<T>(reinterpret_cast<ULONG_PTR>(base) + rva);
}

template <typename T>
constexpr T DataDirectoryFromModuleBase(void* moduleBase, size_t entryId) {
    auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    auto* ntHeader = RvaToVa<PIMAGE_NT_HEADERS>(moduleBase, dosHeader->e_lfanew);
    auto dataDirectory = ntHeader->OptionalHeader.DataDirectory;
    return RvaToVa<T>(moduleBase, dataDirectory[entryId].VirtualAddress);
}

PIMAGE_THUNK_DATA FindAddressByOrdinal(PIMAGE_THUNK_DATA importNames, PIMAGE_THUNK_DATA importAddresses, uint16_t ordinal) {
    for (; importNames->u1.Ordinal; ++importNames, ++importAddresses) {
        if (IMAGE_SNAP_BY_ORDINAL(importNames->u1.Ordinal) && IMAGE_ORDINAL(importNames->u1.Ordinal) == ordinal) {
            return importAddresses;
        }
    }
    return nullptr;
}

PIMAGE_THUNK_DATA FindDelayLoadThunkInModule(void* moduleBase, const char* dllName, uint16_t ordinal) {
    auto* imports = DataDirectoryFromModuleBase<PIMAGE_DELAYLOAD_DESCRIPTOR>(moduleBase, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    for (; imports->DllNameRVA; ++imports) {
        if (_stricmp(RvaToVa<LPCSTR>(moduleBase, imports->DllNameRVA), dllName) != 0) {
            continue;
        }
        auto* importNames = RvaToVa<PIMAGE_THUNK_DATA>(moduleBase, imports->ImportNameTableRVA);
        auto* importAddresses = RvaToVa<PIMAGE_THUNK_DATA>(moduleBase, imports->ImportAddressTableRVA);
        return FindAddressByOrdinal(importNames, importAddresses, ordinal);
    }
    return nullptr;
}

HTHEME WINAPI OpenNcThemeDataDarkScrollBarHook(HWND hWnd, LPCWSTR classList) {
    if (classList && wcscmp(classList, L"ScrollBar") == 0) {
        hWnd = nullptr;
        classList = L"Explorer::ScrollBar";
    }
    return g_openNcThemeData ? g_openNcThemeData(hWnd, classList) : nullptr;
}

void EnsureDarkScrollBarHookInstalled() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hUxtheme) {
        return;
    }
    g_refreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104)));
    g_allowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));
    auto ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
    g_setPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(ord135);
    if (!g_setPreferredAppMode) {
        g_allowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(ord135);
    }

    if (g_setPreferredAppMode) {
        g_setPreferredAppMode(AppModeAllowDark);
    } else if (g_allowDarkModeForApp) {
        g_allowDarkModeForApp(true);
    }
    if (g_refreshImmersiveColorPolicyState) {
        g_refreshImmersiveColorPolicyState();
    }

    g_openNcThemeData = reinterpret_cast<fnOpenNcThemeData>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(49)));
    if (!g_openNcThemeData) {
        return;
    }

    HMODULE hComctl = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hComctl) {
        return;
    }

    auto* address = FindDelayLoadThunkInModule(hComctl, "uxtheme.dll", 49);
    if (!address) {
        return;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE, &oldProtect)) {
        return;
    }

    address->u1.Function = reinterpret_cast<ULONG_PTR>(OpenNcThemeDataDarkScrollBarHook);
    VirtualProtect(address, sizeof(IMAGE_THUNK_DATA), oldProtect, &oldProtect);
}

void ApplyDarkScrollBar(HWND control, bool applyExplorerTheme = true) {
    if (!control) {
        return;
    }
    EnsureDarkScrollBarHookInstalled();
    if (g_allowDarkModeForWindow) {
        g_allowDarkModeForWindow(control, true);
    }
    if (applyExplorerTheme) {
        SetWindowTheme(control, L"Explorer", nullptr);
    }
    SendMessageW(control, WM_THEMECHANGED, 0, 0);
    SetWindowPos(
        control,
        nullptr,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );
    RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

void LoadLanguageSetting(const std::wstring& settingsPath) {
    wchar_t value[32] = {};
    GetPrivateProfileStringW(
        LANGUAGE_SETTINGS_SECTION,
        LANGUAGE_SETTINGS_KEY,
        L"",
        value,
        static_cast<DWORD>(_countof(value)),
        settingsPath.c_str()
    );
    if (value[0] == L'\0') {
        return;
    }
    Localization::SetCurrentLanguageCode(ParseLanguageCodeSetting(value));
}

void LoadScriptInputModeSetting(const std::wstring& settingsPath, bool* allTextInputMode) {
    if (!allTextInputMode) {
        return;
    }
    wchar_t value[64] = {};
    GetPrivateProfileStringW(
        LANGUAGE_SETTINGS_SECTION,
        SCRIPT_INPUT_SETTINGS_KEY,
        L"",
        value,
        static_cast<DWORD>(_countof(value)),
        settingsPath.c_str()
    );
    *allTextInputMode = ParseScriptInputAllTextSetting(value);
}

void LoadDisableFullscreenHotkeysSetting(const std::wstring& settingsPath, bool* disabled) {
    if (!disabled) {
        return;
    }
    *disabled = GetPrivateProfileIntW(
        LANGUAGE_SETTINGS_SECTION,
        DISABLE_FULLSCREEN_HOTKEYS_SETTINGS_KEY,
        0,
        settingsPath.c_str()
    ) != 0;
}

bool SaveLanguageSetting(const std::wstring& settingsPath) {
    const std::wstring value = Localization::GetCurrentLanguageCode().empty()
        ? L"ru"
        : Localization::GetCurrentLanguageCode();
    return WritePrivateProfileStringW(
        LANGUAGE_SETTINGS_SECTION,
        LANGUAGE_SETTINGS_KEY,
        value.c_str(),
        settingsPath.c_str()
    ) != FALSE;
}

bool SaveScriptInputModeSetting(const std::wstring& settingsPath, bool allTextInputMode) {
    return WritePrivateProfileStringW(
        LANGUAGE_SETTINGS_SECTION,
        SCRIPT_INPUT_SETTINGS_KEY,
        allTextInputMode ? SCRIPT_INPUT_MODE_ALL_TEXT : SCRIPT_INPUT_MODE_PREVIOUS_WORD,
        settingsPath.c_str()
    ) != FALSE;
}

bool SaveDisableFullscreenHotkeysSetting(const std::wstring& settingsPath, bool disabled) {
    return WritePrivateProfileStringW(
        LANGUAGE_SETTINGS_SECTION,
        DISABLE_FULLSCREEN_HOTKEYS_SETTINGS_KEY,
        disabled ? L"1" : L"0",
        settingsPath.c_str()
    ) != FALSE;
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
        return T(L"menu.more.logs");
    case ID_MENU_MORE_ABOUT:
        return T(L"menu.more.about");
    case ID_MENU_CONTEXT_COPY:
        return T(L"menu.copy");
    case ID_MENU_CONTEXT_SAVEAS:
        return T(L"menu.save_as");
    case ID_MENU_CONTEXT_CLEAR_LOGS:
        return T(L"menu.clear_logs");
    case ID_MENU_SCRIPTS_ADD:
        return T(L"menu.scripts.add");
    case ID_MENU_SCRIPTS_IMPORT_ZIP:
        return T(L"menu.scripts.import_zip");
    case ID_MENU_SCRIPTS_EXPORT_ZIP:
        return T(L"menu.scripts.export_zip");
    case ID_MENU_SCRIPTS_ENABLE:
        return T(L"menu.scripts.enable");
    case ID_MENU_SCRIPTS_DISABLE:
        return T(L"menu.scripts.disable");
    case ID_MENU_SCRIPTS_DELETE:
        return T(L"menu.scripts.delete");
    case ID_MENU_TRAY_EXIT:
        return T(L"menu.tray.exit");
    case ID_MENU_LANGUAGE_LABEL:
        return T(L"menu.language.title");
    case ID_MENU_INPUT_MODE_LABEL:
        return T(L"menu.input_mode.title");
    case ID_MENU_INPUT_MODE_PREVIOUS_WORD:
        return T(L"menu.input_mode.previous_word");
    case ID_MENU_INPUT_MODE_ALL_TEXT:
        return T(L"menu.input_mode.all_text");
    case ID_MENU_APPLICATION_BLACKLIST:
        return T(L"menu.application_blacklist");
    case ID_MENU_MORE_SEPARATOR:
        return L"";
    default:
        {
            const auto it = g_languageMenuTextById.find(itemId);
            return it != g_languageMenuTextById.end() ? it->second.c_str() : L"";
        }
    }
}

bool IsSubmenuHeaderMenuItem(UINT itemId) {
    return itemId == ID_MENU_LANGUAGE_LABEL || itemId == ID_MENU_INPUT_MODE_LABEL;
}

bool IsStyledMenuItem(UINT itemId) {
    return itemId == ID_MENU_MORE_LOGS || itemId == ID_MENU_MORE_ABOUT
        || itemId == ID_MENU_CONTEXT_COPY || itemId == ID_MENU_CONTEXT_SAVEAS
        || itemId == ID_MENU_CONTEXT_CLEAR_LOGS
        || itemId == ID_MENU_SCRIPTS_ADD || itemId == ID_MENU_SCRIPTS_IMPORT_ZIP
        || itemId == ID_MENU_SCRIPTS_EXPORT_ZIP || itemId == ID_MENU_SCRIPTS_ENABLE
        || itemId == ID_MENU_SCRIPTS_DISABLE || itemId == ID_MENU_SCRIPTS_DELETE
        || itemId == ID_MENU_LANGUAGE_LABEL
        || itemId == ID_MENU_INPUT_MODE_LABEL
        || itemId == ID_MENU_INPUT_MODE_PREVIOUS_WORD
        || itemId == ID_MENU_INPUT_MODE_ALL_TEXT
        || itemId == ID_MENU_APPLICATION_BLACKLIST
        || itemId == ID_MENU_TRAY_EXIT
        || itemId == ID_MENU_MORE_SEPARATOR
        || IsDynamicLanguageMenuId(itemId);
}

const wchar_t* GetInfoWindowTitleByKind(int kind) {
    if (kind == 1) {
        return GetMenuItemText(ID_MENU_MORE_ABOUT);
    }
    if (kind == 2) {
        return GetMenuItemText(ID_MENU_MORE_LOGS);
    }
    if (kind == 3) {
        return T(L"application_blacklist.title");
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
    const UINT extraWidth = IsSubmenuHeaderMenuItem(itemId) ? 48 : 34;
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
        textRect.right -= IsSubmenuHeaderMenuItem(itemId) ? 28 : 10;
        SetBkMode(dis->hDC, TRANSPARENT);
        const COLORREF textColor = disabled ? RGB(120, 120, 120) : RGB(235, 235, 235);
        SetTextColor(dis->hDC, textColor);

        if (checked) {
            UiRenderer::DrawMenuCheckMark(dis->hDC, dis->rcItem, textColor);
        }

        HFONT menuFont = GetMenuFontForItem(itemId);
        HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, menuFont));
        DrawTextW(dis->hDC, GetMenuItemText(itemId), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (IsSubmenuHeaderMenuItem(itemId)) {
            const COLORREF arrowColor = disabled
                ? RGB(95, 95, 95)
                : (selected ? RGB(175, 175, 175) : RGB(128, 128, 128));
            UiRenderer::DrawMenuChevron(dis->hDC, dis->rcItem, arrowColor);
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

LRESULT CALLBACK CheckboxPaintSubclassProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR refData
) {
    auto* visualState = reinterpret_cast<CheckboxVisualState*>(refData);
    if (message == WM_MOUSEMOVE && visualState && !visualState->hot) {
        visualState->hot = true;
        TRACKMOUSEEVENT tracking = { sizeof(tracking), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tracking);
        InvalidateRect(hWnd, nullptr, FALSE);
    } else if (message == WM_MOUSELEAVE && visualState) {
        visualState->hot = false;
        InvalidateRect(hWnd, nullptr, FALSE);
    }

    if (message == WM_PAINT) {
        PAINTSTRUCT paint = {};
        HDC hdc = BeginPaint(hWnd, &paint);
        const int length = GetWindowTextLengthW(hWnd);
        std::wstring label(static_cast<size_t>(length) + 1, L'\0');
        if (length > 0) {
            GetWindowTextW(hWnd, label.data(), length + 1);
        }
        label.resize(static_cast<size_t>(length));
        const LRESULT state = SendMessageW(hWnd, BM_GETSTATE, 0, 0);
        UiRenderer::DrawCustomCheckbox(
            hdc,
            hWnd,
            label,
            SendMessageW(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED,
            visualState && visualState->hot,
            (state & BST_PUSHED) != 0,
            IsWindowEnabled(hWnd) != FALSE,
            GetFocus() == hWnd
        );
        EndPaint(hWnd, &paint);
        return 0;
    }

    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, CheckboxPaintSubclassProc, subclassId);
        delete visualState;
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }

    const LRESULT result = DefSubclassProc(hWnd, message, wParam, lParam);
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP
        || message == WM_KEYDOWN || message == WM_KEYUP
        || message == WM_SETFOCUS || message == WM_KILLFOCUS
        || message == WM_ENABLE || message == WM_SETTEXT
        || message == BM_SETCHECK || message == BM_SETSTATE) {
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    return result;
}

void PaintDarkListViewHeader(HWND header, HDC hdc) {
    const HFONT headerFont = reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = headerFont ? SelectObject(hdc, headerFont) : nullptr;
    const HGDIOBJ previousBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    const HGDIOBJ previousPen = SelectObject(hdc, GetStockObject(DC_PEN));

    RECT clientRect = {};
    GetClientRect(header, &clientRect);
    SetDCBrushColor(hdc, RGB(45, 45, 45));
    FillRect(hdc, &clientRect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    POINT cursor = {};
    GetCursorPos(&cursor);
    ScreenToClient(header, &cursor);
    const bool pressed = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
    const int count = Header_GetItemCount(header);
    for (int index = 0; index < count; ++index) {
        RECT cellRect = {};
        if (!Header_GetItemRect(header, index, &cellRect)) {
            continue;
        }

        std::wstring text(1024, L'\0');
        HDITEMW item = {};
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = text.data();
        item.cchTextMax = static_cast<int>(text.size());
        Header_GetItem(header, index, &item);
        text.resize(wcslen(text.c_str()));

        const bool hot = PtInRect(&cellRect, cursor) != FALSE;
        SetDCBrushColor(hdc, hot ? (pressed ? RGB(68, 68, 68) : RGB(58, 58, 58)) : RGB(45, 45, 45));
        FillRect(hdc, &cellRect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

        RECT textRect = cellRect;
        textRect.left += LIST_TEXT_PADDING;
        textRect.right -= LIST_TEXT_PADDING;
        const bool sortedUp = (item.fmt & HDF_SORTUP) != 0;
        const bool sortedDown = (item.fmt & HDF_SORTDOWN) != 0;
        if (sortedUp || sortedDown) {
            textRect.right -= 16;
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(245, 245, 245));
        DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (sortedUp || sortedDown) {
            const int centerX = cellRect.right - LIST_TEXT_PADDING - 4;
            const int centerY = (cellRect.top + cellRect.bottom) / 2;
            POINT triangle[3] = {
                { centerX - 3, sortedUp ? centerY + 2 : centerY - 2 },
                { centerX + 3, sortedUp ? centerY + 2 : centerY - 2 },
                { centerX, sortedUp ? centerY - 2 : centerY + 2 }
            };
            SetDCBrushColor(hdc, RGB(245, 245, 245));
            SetDCPenColor(hdc, RGB(245, 245, 245));
            Polygon(hdc, triangle, 3);
        }

        SetDCBrushColor(hdc, RGB(72, 72, 72));
        RECT separator = { cellRect.right - 1, cellRect.top, cellRect.right, cellRect.bottom };
        FillRect(hdc, &separator, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    SetDCBrushColor(hdc, RGB(72, 72, 72));
    RECT border = { clientRect.left, clientRect.bottom - 1, clientRect.right, clientRect.bottom };
    FillRect(hdc, &border, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    SelectObject(hdc, previousPen);
    SelectObject(hdc, previousBrush);
    if (previousFont) {
        SelectObject(hdc, previousFont);
    }
}

LRESULT CALLBACK DarkHeaderSubclassProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR
) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint = {};
        HDC hdc = BeginPaint(hWnd, &paint);
        PaintDarkListViewHeader(hWnd, hdc);
        EndPaint(hWnd, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, DarkHeaderSubclassProc, subclassId);
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }

    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking = { sizeof(tracking), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tracking);
    }
    const LRESULT result = DefSubclassProc(hWnd, message, wParam, lParam);
    if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE
        || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    return result;
}

void ApplyDarkListViewHeader(HWND listView) {
    if (!listView) {
        return;
    }
    HWND header = ListView_GetHeader(listView);
    if (!header) {
        return;
    }
    SendMessageW(header, WM_SETFONT, SendMessageW(listView, WM_GETFONT, 0, 0), TRUE);
    SetWindowSubclass(header, DarkHeaderSubclassProc, DARK_HEADER_SUBCLASS_ID, 0);
    InvalidateRect(header, nullptr, TRUE);
}
}

Application::Application() = default;
Application* Application::s_morePopupMouseHookOwner = nullptr;

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_initializationError.clear();
    const std::wstring executableDirectory = GetExecutableDirectory();
    m_logPath = executableDirectory + L"\\" + LOG_FILE_NAME;

    Localization::Initialize(executableDirectory + L"\\lang");
    const std::wstring settingsPath = GetLanguageSettingsPath(executableDirectory);
    LoadLanguageSetting(settingsPath);
    LoadScriptInputModeSetting(settingsPath, &m_scriptInputAllText);
    LoadDisableFullscreenHotkeysSetting(settingsPath, &m_disableHotkeysInFullscreen);

    m_singleInstanceMutex = CreateMutexW(nullptr, FALSE, SINGLE_INSTANCE_MUTEX_NAME);
    if (m_singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWindow = FindWindowW(WINDOW_CLASS_NAME, nullptr);
        if (existingWindow && IsWindow(existingWindow)) {
            ShowWindow(existingWindow, SW_SHOWNORMAL);
            SetForegroundWindow(existingWindow);
        }
        m_initializationError = INIT_ERROR_ALREADY_RUNNING;
        return false;
    }
    LogFile::Clear(m_logPath);

    m_blacklistPath = executableDirectory + L"\\TextMagic.blacklist";
    std::wstring blacklistLoadError;
    const bool blacklistLoaded = m_applicationBlacklist.Load(m_blacklistPath, &blacklistLoadError);
    g_applicationBlacklist = &m_applicationBlacklist;
    g_scriptExecutionGate = &m_scriptExecutionGate;
    g_disableHotkeysInFullscreen = m_disableHotkeysInFullscreen;
    InvalidateForegroundBlockCache();

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        m_initializationError = T(L"app.error.init.gdiplus");
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
    m_msfteditModule = LoadLibraryW(L"Msftedit.dll");

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
        m_initializationError = T(L"app.error.init.main_class");
        return false;
    }

    if (!RegisterInfoWindowClass()) {
        m_initializationError = T(L"app.error.init.info_class");
        return false;
    }
    if (!RegisterMessageWindowClass()) {
        m_initializationError = T(L"app.error.init.message_class");
        return false;
    }
    if (!RegisterMorePopupWindowClass()) {
        m_initializationError = T(L"app.error.init.main_window");
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
        m_initializationError = T(L"app.error.init.main_window");
        return false;
    }
    SetWindowTextW(m_hWnd, WINDOW_TITLE);
    DragAcceptFiles(m_hWnd, TRUE);
    InitializeTrayIcon();
    SetHotkeyDispatchWindow(m_hWnd);
    if (!InstallInputHooks(m_hInstance)) {
        m_initializationError = T(L"app.error.init.keyboard_hook");
        Shutdown();
        return false;
    }
    ClearInputBuffer();

    CreateControls();
    ApplyLocalization();

    RECT clientRect = {};
    GetClientRect(m_hWnd, &clientRect);
    OnResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);

    m_scriptsDirectory = executableDirectory + L"\\scripts";
    std::error_code createDirError;
    fs::create_directories(fs::path(m_scriptsDirectory), createDirError);

    AppendLog(std::wstring(T(L"app.log.starting_prefix")) + WINDOW_TITLE + L" " + APP_VERSION + L".");
    if (!blacklistLoaded) {
        AppendLog(std::wstring(T(L"app.log.blacklist_load_warning_prefix")) + L" " + blacklistLoadError);
    }
    AppendLog(std::wstring(T(L"app.log.scripts_dir_prefix")) + m_scriptsDirectory);
    ReloadScripts(true);
    return true;
}

int Application::Run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (m_toolTip) {
            m_toolTip->RelayEvent(msg);
        }
        const HWND activeInfoWindow = GetActiveWindow();
        if (activeInfoWindow
            && (activeInfoWindow == m_hAboutWindow
                || activeInfoWindow == m_hLogsWindow
                || activeInfoWindow == m_hApplicationBlacklistWindow)
            && IsDialogMessageW(activeInfoWindow, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void Application::Shutdown() {
    SetHotkeyDispatchWindow(nullptr);
    g_applicationBlacklist = nullptr;
    g_scriptExecutionGate = nullptr;
    g_disableHotkeysInFullscreen = false;
    InvalidateForegroundBlockCache();
    UninstallInputHooks();
    ClearInputBuffer();
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
    if (m_hApplicationBlacklistWindow && IsWindow(m_hApplicationBlacklistWindow)) {
        DestroyWindow(m_hApplicationBlacklistWindow);
        m_hApplicationBlacklistWindow = nullptr;
    }
    CloseMorePopupWindows();

    if (m_msfteditModule) {
        FreeLibrary(m_msfteditModule);
        m_msfteditModule = nullptr;
    }

    m_toolTip.reset();

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

    if (m_singleInstanceMutex) {
        CloseHandle(m_singleInstanceMutex);
        m_singleInstanceMutex = nullptr;
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

    case WM_SCRIPT_EXECUTION_COMPLETE:
        [&]() {
            std::unique_ptr<ScriptExecutionTaskResult> result(reinterpret_cast<ScriptExecutionTaskResult*>(wParam));
            if (!result) {
                return;
            }

            std::wstring sourceName = result->clipboardMode
                ? T(L"app.status.clipboard")
                : (result->hasSelection
                    ? T(L"app.status.selection")
                    : (result->inputBufferMode && !result->allTextInputMode
                        ? T(L"app.status.previous_word") : T(L"app.status.full_text")));
            AppendLog(std::wstring(T(L"app.log.script.source_prefix")) + sourceName
                + T(L"app.log.script.source_chars_prefix") + std::to_wstring(result->sourceText.size()) + L".");

            if (!result->executeOk) {
                if (result->noTextAvailable) {
                    const std::wstring msg = T(L"app.status.no_text_available");
                    SetStatusText(msg);
                    AppendLog(std::wstring(T(L"app.log.script.error_prefix2")) + msg);
                    return;
                }
                if (result->inputBufferMode && !result->allTextInputMode) {
                    AppendLog(T(L"app.log.script.previous_word_not_changed"));
                }
                const std::wstring statusMessage = std::wstring(T(L"app.status.script_error_prefix")) + result->scriptName;
                const std::wstring dialogMessage = std::wstring(T(L"app.status.script_label")) + result->scriptName
                    + T(L"app.status.error_label_block") + result->executionError;
                SetStatusText(statusMessage);
                AppendLog(std::wstring(T(L"app.log.script.error_prefix")) + result->scriptName + L"\": " + result->executionError);
                ShowStyledMessage(T(L"app.title.execution_error"), dialogMessage);
                return;
            }

            bool replaceOk = false;
            if (result->clipboardMode) {
                replaceOk = ClipboardUtils::WriteText(m_hWnd, result->outputText);
            } else if (result->inputBufferMode) {
                const auto& capture = result->inputCapture;
                const std::wstring replacement = result->outputText + capture.trailing;
                const std::wstring capturedText = capture.word + capture.trailing;
                const HWND expectedTarget = result->inputTargetWindow;

                if (!result->inputReady || !IsPreviousWordCaptureCurrent(capture)) {
                    ClearInputBuffer();
                } else if (m_textBridge.ReplaceText(
                               expectedTarget, capturedText, replacement)) {
                    replaceOk = CommitPreviousWordReplacement(capture, replacement);
                    if (!replaceOk) {
                        ClearInputBuffer();
                    }
                } else {
                    ClearInputBuffer();
                }
            } else {
                replaceOk = m_textBridge.SetSelectedText(result->outputText);
            }
            if (!replaceOk) {
                const std::wstring msg = result->clipboardMode
                    ? T(L"app.status.clipboard_write_failed")
                    : T(L"app.status.active_control_paste_failed");
                SetStatusText(msg);
                AppendLog(std::wstring(T(L"app.log.script.error_prefix")) + result->scriptName + L"\": " + msg);
                ShowStyledMessage(T(L"app.title.paste_error"), msg);
                return;
            }
            std::wstring status = std::wstring(T(L"app.status.script_applied_prefix")) + result->scriptName + T(L"app.status.script_applied_middle");
            if (result->clipboardMode) {
                status += T(L"app.status.clipboard_text");
            } else if (result->inputBufferMode) {
                status += result->allTextInputMode
                    ? T(L"app.status.all_text")
                    : T(L"app.status.previous_word_text");
            } else {
                status += result->hasSelection
                    ? T(L"app.status.selected_text")
                    : T(L"app.status.all_text");
            }
            SetStatusText(status);
            AppendLog(std::wstring(T(L"app.log.script.done_prefix")) + result->scriptName + T(L"app.log.script.result_prefix")
                + std::to_wstring(result->outputText.size()) + T(L"app.log.script.result_suffix"));
            AppendLog(std::wstring(T(L"app.log.script.prefix")) + status);
        }();
        m_scriptExecutionGate.Release(GetTickCount64());
        return SCRIPT_EXECUTION_COMPLETE_HANDLED;

    case WM_UPDATE_CHECK_COMPLETE:
        {
            std::unique_ptr<UpdateCheckTaskResult> result(reinterpret_cast<UpdateCheckTaskResult*>(wParam));
            m_updateInProgress = false;
            if (!result) {
                return 0;
            }

            if (!result->check.success) {
                AppendLog(std::wstring(T(L"app.log.update.error_prefix")) + result->check.errorMessage);
                ShowStyledMessage(T(L"app.title.update"), std::wstring(T(L"app.status.update_check_error_prefix")) + result->check.errorMessage);
                return 0;
            }

            if (!result->check.updateAvailable) {
                AppendLog(T(L"app.log.update.no_new"));
                ShowStyledMessage(T(L"app.title.update"), std::wstring(T(L"app.status.latest_version_prefix")) + APP_VERSION);
                return 0;
            }

            const std::wstring prompt =
                std::wstring(T(L"app.status.update_available_prefix")) + result->check.latestVersion + L" (" + result->check.latestTag
                + T(L"app.status.update_available_suffix");
            const int decision = ShowStyledMessageDialog(T(L"app.title.update"), prompt, T(L"app.button.update"), T(L"app.button.later"));
            if (decision != IDYES) {
                AppendLog(T(L"app.log.update.postponed"));
                return 0;
            }

            const std::wstring latestTag = result->check.latestTag;
            const std::wstring targetPath = GetExecutablePath();
            const std::wstring tmpPath = GetExecutableDirectory() + L"\\" + TM_APP_NAME_W + L".update.tmp.exe";

            m_updateInProgress = true;
            const HWND windowHandle = m_hWnd;
            const UpdateService updateService = m_updateService;
            std::thread([windowHandle, updateService, latestTag, targetPath, tmpPath]() {
                auto* installResult = new UpdateInstallTaskResult();
                std::wstring error;
                if (!updateService.DownloadReleaseExecutable(latestTag, tmpPath, error)) {
                    installResult->success = false;
                    installResult->error = error;
                    PostOwnedMessage(windowHandle, WM_UPDATE_INSTALL_COMPLETE, installResult);
                    return;
                }
                if (!updateService.LaunchUpdaterProcess(GetCurrentProcessId(), tmpPath, targetPath, error)) {
                    installResult->success = false;
                    installResult->error = error;
                    PostOwnedMessage(windowHandle, WM_UPDATE_INSTALL_COMPLETE, installResult);
                    return;
                }
                installResult->success = true;
                PostOwnedMessage(windowHandle, WM_UPDATE_INSTALL_COMPLETE, installResult);
            }).detach();
        }
        return 0;

    case WM_UPDATE_INSTALL_COMPLETE:
        {
            std::unique_ptr<UpdateInstallTaskResult> result(reinterpret_cast<UpdateInstallTaskResult*>(wParam));
            m_updateInProgress = false;
            if (!result) {
                return 0;
            }
            if (!result->success) {
                AppendLog(std::wstring(T(L"app.log.update.error_prefix")) + result->error);
                ShowStyledMessage(T(L"app.title.update"), std::wstring(T(L"app.status.update_finish_failed_prefix")) + result->error);
                return 0;
            }
            AppendLog(T(L"app.log.update.started"));
            ShowStyledMessage(T(L"app.title.update"), T(L"app.status.update_downloaded_restart"));
            m_isExiting = true;
            PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_IMPORT_ZIP_COMPLETE:
        {
            std::unique_ptr<ImportZipTaskResult> result(reinterpret_cast<ImportZipTaskResult*>(wParam));
            m_archiveTaskInProgress = false;
            if (!result) {
                return 0;
            }
            if (!result->success) {
                ShowStyledMessage(T(L"app.title.import_error"), result->errorMessage);
                return 0;
            }
            for (const auto& copiedPath : result->importedPaths) {
                AppendLog(std::wstring(T(L"app.log.scripts.imported_prefix")) + copiedPath);
            }
            if (result->importedCount > 0) {
                ReloadScripts(false);
                const std::wstring status =
                    std::wstring(T(L"app.status.scripts_added_prefix")) + std::to_wstring(result->importedCount)
                    + T(L"app.status.skipped_prefix") + std::to_wstring(result->skippedCount) + L".";
                SetStatusText(status);
                AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
            } else {
                const std::wstring status = T(L"app.status.no_suitable_import");
                SetStatusText(status);
                AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
            }
        }
        return 0;

    case WM_EXPORT_ZIP_COMPLETE:
        {
            std::unique_ptr<ExportZipTaskResult> result(reinterpret_cast<ExportZipTaskResult*>(wParam));
            m_archiveTaskInProgress = false;
            if (!result) {
                return 0;
            }
            if (!result->success) {
                ShowStyledMessage(T(L"app.title.export_error"), std::wstring(T(L"app.status.zip_create_failed_prefix")) + result->errorMessage);
                return 0;
            }
            const std::wstring status = std::wstring(T(L"app.status.exported_prefix")) + std::to_wstring(result->scriptFileCount)
                + T(L"app.status.exported_suffix") + result->archivePath;
            SetStatusText(status);
            AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
        }
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
        if (m_hMorePopupWindow) {
            POINT cursorPos = {};
            GetCursorPos(&cursorPos);
            HandleMorePopupMouseDown(cursorPos);
        }
        if (m_hoveredControl) {
            m_pressedControl = m_hoveredControl;
            InvalidateRect(m_pressedControl, nullptr, TRUE);
        }
        break;

    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
    case WM_NCMBUTTONDOWN:
        if (m_hMorePopupWindow) {
            POINT cursorPos = {};
            GetCursorPos(&cursorPos);
            HandleMorePopupMouseDown(cursorPos);
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
            if (DrawPaddedListBoxItem(dis)) {
                return TRUE;
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

    case WM_TIMER:
        if (wParam == MORE_POPUP_TRACK_TIMER_ID) {
            UpdateMorePopupTracking();
            return 0;
        }
        break;

    case WM_ACTIVATEAPP:
        if (!wParam) {
            CloseMorePopupWindows();
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam == VK_ESCAPE && m_hMorePopupWindow) {
            CloseMorePopupWindows();
            return 0;
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

    m_hTitleLabel = CreateWindowExW(0, L"STATIC", WINDOW_TITLE, WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 100, 30, m_hWnd, reinterpret_cast<HMENU>(ID_TITLE_LABEL), m_hInstance, nullptr);

    m_hHintLabel = CreateWindowExW(0, L"STATIC",
        T(L"hint.label"),
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 40,
        m_hWnd, reinterpret_cast<HMENU>(ID_HINT_LABEL), m_hInstance, nullptr);

    m_hScriptList = CreateWindowExW(0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT
            | LBS_EXTENDEDSEL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        0, 0, 100, 100, m_hWnd, reinterpret_cast<HMENU>(ID_SCRIPTS_LIST), m_hInstance, nullptr);
    ApplyDarkScrollBar(m_hScriptList);

    m_hReloadButton = CreateWindowExW(0, L"BUTTON", T(L"button.reload_scripts"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 100, 32, m_hWnd, reinterpret_cast<HMENU>(ID_RELOAD_BUTTON), m_hInstance, nullptr);

    m_hOpenFolderButton = CreateWindowExW(0, L"BUTTON", T(L"button.open_scripts_folder"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 100, 32, m_hWnd, reinterpret_cast<HMENU>(ID_OPEN_FOLDER_BUTTON), m_hInstance, nullptr);

    m_hMoreButton = CreateWindowExW(0, L"BUTTON", T(L"button.more"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 100, 32, m_hWnd, reinterpret_cast<HMENU>(ID_MORE_BUTTON), m_hInstance, nullptr);

    m_hStatusLabel = CreateWindowExW(0, L"STATIC", T(L"status.ready"),
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24,
        m_hWnd, reinterpret_cast<HMENU>(ID_STATUS_LABEL), m_hInstance, nullptr);

    SendMessageW(m_hTitleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hTitleFont), TRUE);
    SendMessageW(m_hHintLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hScriptList, WM_SETFONT, reinterpret_cast<WPARAM>(m_hMonoFont), TRUE);
    SendMessageW(m_hScriptList, LB_SETITEMHEIGHT, 0, LIST_ITEM_HEIGHT);
    SendMessageW(m_hReloadButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hOpenFolderButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hMoreButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hStatusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);

    m_toolTip = std::make_unique<ToolTip>();
    if (m_toolTip->Initialize(m_hWnd)) {
        m_toolTip->SetStyle(m_hFont, RGB(50, 50, 50), RGB(240, 240, 240));
        m_toolTip->AddTool(m_hReloadButton, T(L"tooltip.reload"));
        m_toolTip->AddTool(m_hScriptList, T(L"tooltip.script_list"));
        m_toolTip->AddTool(m_hOpenFolderButton, T(L"tooltip.open_scripts_folder"));
        m_toolTip->AddTool(m_hMoreButton, T(L"tooltip.more"));
    }
}

void Application::CreateMoreMenu() {
    CloseMorePopupWindows();
    ClearDynamicLanguageMenuItems();

    const std::vector<std::wstring> languageCodes = Localization::GetAvailableLanguageCodes();
    UINT nextLanguageMenuId = ID_MENU_LANGUAGE_DYNAMIC_FIRST;
    for (const std::wstring& languageCode : languageCodes) {
        if (nextLanguageMenuId > ID_MENU_LANGUAGE_DYNAMIC_LAST) {
            break;
        }

        std::wstring displayName = Localization::GetLanguageDisplayName(languageCode);
        if (displayName.empty()) {
            displayName = languageCode;
        }
        g_languageMenuTextById[nextLanguageMenuId] = displayName;
        g_languageMenuCodeById[nextLanguageMenuId] = languageCode;
        ++nextLanguageMenuId;
    }

    if (g_languageMenuTextById.empty()) {
        const UINT fallbackId = ID_MENU_LANGUAGE_DYNAMIC_FIRST;
        const std::wstring fallbackCode = L"ru";
        g_languageMenuTextById[fallbackId] = Localization::GetLanguageDisplayName(fallbackCode);
        g_languageMenuCodeById[fallbackId] = fallbackCode;
    }
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
    MoveWindow(
        m_hScriptList,
        innerX + LIST_CONTENT_PADDING,
        listTop + LIST_CONTENT_PADDING,
        std::max(1, innerWidth - 2 * LIST_CONTENT_PADDING),
        std::max(1, listHeight - 2 * LIST_CONTENT_PADDING),
        TRUE
    );

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
    UiRenderer::DrawEditBorder(m_hWnd, m_hScriptList, LIST_CONTENT_PADDING);
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
    std::wstring selectedLanguageCode;
    if (TryGetLanguageCodeByMenuId(menuId, &selectedLanguageCode)) {
        SetLanguage(selectedLanguageCode);
        return;
    }

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
    case ID_MENU_INPUT_MODE_PREVIOUS_WORD:
        SetScriptInputMode(false);
        break;
    case ID_MENU_INPUT_MODE_ALL_TEXT:
        SetScriptInputMode(true);
        break;
    case ID_MENU_APPLICATION_BLACKLIST:
        ShowApplicationBlacklistWindow();
        break;
    case ID_MENU_TRAY_EXIT:
        ExitApplication();
        break;
    default:
        break;
    }
}

void Application::ShowMoreMenu() {
    if (m_hMorePopupWindow && IsWindow(m_hMorePopupWindow)) {
        CloseMorePopupWindows();
        return;
    }

    m_morePopupItems = BuildMainMorePopupItems();
    if (m_morePopupItems.empty() || !m_hWnd || !m_hMoreButton) {
        return;
    }

    m_activeMoreSubMenuHeaderId = 0;
    m_hoveredMorePopupItemId = 0;
    m_hoveredMoreSubPopupItemId = 0;
    m_moreSubPopupItems.clear();

    const SIZE popupSize = MeasureMorePopupWindow(m_morePopupItems);
    RECT rect = {};
    GetWindowRect(m_hMoreButton, &rect);
    m_hMorePopupWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        MORE_POPUP_WINDOW_CLASS_NAME,
        L"",
        WS_POPUP,
        rect.left,
        rect.bottom + 2,
        popupSize.cx,
        popupSize.cy,
        m_hWnd,
        nullptr,
        m_hInstance,
        this
    );
    if (!m_hMorePopupWindow) {
        m_morePopupItems.clear();
        return;
    }

    SetWindowPos(
        m_hMorePopupWindow,
        HWND_TOPMOST,
        rect.left,
        rect.bottom + 2,
        popupSize.cx,
        popupSize.cy,
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );
    StartMoreMenuTracking();
    UpdateMorePopupTracking();
}

void Application::StartMoreMenuTracking() {
    if (m_hWnd) {
        SetTimer(m_hWnd, MORE_POPUP_TRACK_TIMER_ID, MORE_POPUP_TRACK_INTERVAL_MS, nullptr);
    }
    if (!m_morePopupMouseHook) {
        s_morePopupMouseHookOwner = this;
        m_morePopupMouseHook = SetWindowsHookExW(WH_MOUSE, MorePopupMouseHookProc, nullptr, GetCurrentThreadId());
    }
}

void Application::StopMoreMenuTracking() {
    if (m_hWnd) {
        KillTimer(m_hWnd, MORE_POPUP_TRACK_TIMER_ID);
    }
    if (m_morePopupMouseHook) {
        UnhookWindowsHookEx(m_morePopupMouseHook);
        m_morePopupMouseHook = nullptr;
    }
    if (s_morePopupMouseHookOwner == this) {
        s_morePopupMouseHookOwner = nullptr;
    }
}

void Application::CloseMorePopupWindows() {
    StopMoreMenuTracking();
    CloseMoreSubPopupWindow();
    if (m_hMorePopupWindow && IsWindow(m_hMorePopupWindow)) {
        DestroyWindow(m_hMorePopupWindow);
    }
}

void Application::CloseMoreSubPopupWindow() {
    if (m_hMoreSubPopupWindow && IsWindow(m_hMoreSubPopupWindow)) {
        DestroyWindow(m_hMoreSubPopupWindow);
    }
}

void Application::EnsureMoreSubPopup(UINT headerItemId) {
    if (!IsSubmenuHeaderMenuItem(headerItemId) || !m_hMorePopupWindow || !IsWindow(m_hMorePopupWindow)) {
        CloseMoreSubPopupWindow();
        return;
    }

    if (headerItemId == m_activeMoreSubMenuHeaderId
        && m_hMoreSubPopupWindow
        && IsWindow(m_hMoreSubPopupWindow)) {
        return;
    }

    int headerIndex = -1;
    for (size_t index = 0; index < m_morePopupItems.size(); ++index) {
        if (m_morePopupItems[index].id == headerItemId) {
            headerIndex = static_cast<int>(index);
            break;
        }
    }
    if (headerIndex < 0) {
        return;
    }

    RECT headerRect = GetMorePopupItemRect(
        m_hMorePopupWindow,
        m_morePopupItems,
        static_cast<size_t>(headerIndex)
    );
    if (IsRectEmpty(&headerRect)) {
        return;
    }

    POINT topLeft = { headerRect.left, headerRect.top };
    POINT bottomRight = { headerRect.right, headerRect.bottom };
    ClientToScreen(m_hMorePopupWindow, &topLeft);
    ClientToScreen(m_hMorePopupWindow, &bottomRight);

    m_moreSubPopupItems = BuildSubMorePopupItems(headerItemId);
    if (m_moreSubPopupItems.empty()) {
        CloseMoreSubPopupWindow();
        return;
    }

    const SIZE popupSize = MeasureMorePopupWindow(m_moreSubPopupItems);
    CloseMoreSubPopupWindow();
    m_activeMoreSubMenuHeaderId = headerItemId;
    m_hMoreSubPopupWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        MORE_POPUP_WINDOW_CLASS_NAME,
        L"",
        WS_POPUP,
        bottomRight.x - 1,
        topLeft.y,
        popupSize.cx,
        popupSize.cy,
        m_hWnd,
        nullptr,
        m_hInstance,
        this
    );
    if (!m_hMoreSubPopupWindow) {
        m_moreSubPopupItems.clear();
        m_activeMoreSubMenuHeaderId = 0;
        return;
    }

    SetWindowPos(
        m_hMoreSubPopupWindow,
        HWND_TOPMOST,
        bottomRight.x - 1,
        topLeft.y,
        popupSize.cx,
        popupSize.cy,
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );
    m_hoveredMoreSubPopupItemId = 0;
}

void Application::UpdateMorePopupTracking() {
    if (!m_hMorePopupWindow || !IsWindow(m_hMorePopupWindow)) {
        StopMoreMenuTracking();
        return;
    }

    POINT cursorPos = {};
    GetCursorPos(&cursorPos);

    const bool isInMainPopup = IsPointInWindow(m_hMorePopupWindow, cursorPos);
    const bool isInSubPopup = IsPointInWindow(m_hMoreSubPopupWindow, cursorPos);
    const bool isInMoreButton = IsPointInMoreButton(cursorPos);

    UINT hoveredMainItemId = 0;
    if (isInMainPopup) {
        const int itemIndex = HitTestMorePopupItem(m_hMorePopupWindow, m_morePopupItems, cursorPos);
        if (itemIndex >= 0) {
            hoveredMainItemId = m_morePopupItems[static_cast<size_t>(itemIndex)].id;
        }
    } else if (m_activeMoreSubMenuHeaderId != 0) {
        hoveredMainItemId = m_activeMoreSubMenuHeaderId;
    }
    if (hoveredMainItemId != m_hoveredMorePopupItemId) {
        m_hoveredMorePopupItemId = hoveredMainItemId;
        if (m_hMorePopupWindow && IsWindow(m_hMorePopupWindow)) {
            InvalidateRect(m_hMorePopupWindow, nullptr, FALSE);
        }
    }

    if (isInMainPopup) {
        const int itemIndex = HitTestMorePopupItem(m_hMorePopupWindow, m_morePopupItems, cursorPos);
        if (itemIndex >= 0) {
            const UiRenderer::PopupMenuItem& item = m_morePopupItems[static_cast<size_t>(itemIndex)];
            if (item.submenu) {
                EnsureMoreSubPopup(item.id);
            } else {
                CloseMoreSubPopupWindow();
            }
        } else if (!isInMoreButton) {
            CloseMoreSubPopupWindow();
        }
    }

    UINT hoveredSubItemId = 0;
    if (isInSubPopup) {
        const int itemIndex = HitTestMorePopupItem(m_hMoreSubPopupWindow, m_moreSubPopupItems, cursorPos);
        if (itemIndex >= 0) {
            hoveredSubItemId = m_moreSubPopupItems[static_cast<size_t>(itemIndex)].id;
        }
    }
    if (hoveredSubItemId != m_hoveredMoreSubPopupItemId) {
        m_hoveredMoreSubPopupItemId = hoveredSubItemId;
        if (m_hMoreSubPopupWindow && IsWindow(m_hMoreSubPopupWindow)) {
            InvalidateRect(m_hMoreSubPopupWindow, nullptr, FALSE);
        }
    }
}

bool Application::HandleMorePopupMouseDown(POINT screenPoint) {
    if (!m_hMorePopupWindow || !IsWindow(m_hMorePopupWindow)) {
        return false;
    }
    if (IsPointInWindow(m_hMorePopupWindow, screenPoint)
        || IsPointInWindow(m_hMoreSubPopupWindow, screenPoint)
        || IsPointInMoreButton(screenPoint)) {
        return false;
    }

    CloseMorePopupWindows();
    return true;
}

LRESULT CALLBACK Application::MorePopupMouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && s_morePopupMouseHookOwner) {
        const auto* mouseInfo = reinterpret_cast<MOUSEHOOKSTRUCT*>(lParam);
        switch (wParam) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
        case WM_NCMBUTTONDOWN:
            if (mouseInfo) {
                s_morePopupMouseHookOwner->HandleMorePopupMouseDown(mouseInfo->pt);
            }
            break;
        default:
            break;
        }
    }

    HHOOK hookHandle = s_morePopupMouseHookOwner ? s_morePopupMouseHookOwner->m_morePopupMouseHook : nullptr;
    return CallNextHookEx(hookHandle, code, wParam, lParam);
}

std::vector<UiRenderer::PopupMenuItem> Application::BuildMainMorePopupItems() const {
    return {
        { ID_MENU_MORE_LOGS, GetMenuItemText(ID_MENU_MORE_LOGS), false, false, false },
        { ID_MENU_MORE_SEPARATOR, L"", true, false, false },
        { ID_MENU_LANGUAGE_LABEL, GetMenuItemText(ID_MENU_LANGUAGE_LABEL), false, false, true },
        { ID_MENU_MORE_SEPARATOR, L"", true, false, false },
        { ID_MENU_INPUT_MODE_LABEL, GetMenuItemText(ID_MENU_INPUT_MODE_LABEL), false, false, true },
        { ID_MENU_MORE_SEPARATOR, L"", true, false, false },
        { ID_MENU_APPLICATION_BLACKLIST, GetMenuItemText(ID_MENU_APPLICATION_BLACKLIST), false, false, false },
        { ID_MENU_MORE_SEPARATOR, L"", true, false, false },
        { ID_MENU_MORE_ABOUT, GetMenuItemText(ID_MENU_MORE_ABOUT), false, false, false }
    };
}

std::vector<UiRenderer::PopupMenuItem> Application::BuildSubMorePopupItems(UINT headerItemId) const {
    std::vector<UiRenderer::PopupMenuItem> items;
    if (headerItemId == ID_MENU_LANGUAGE_LABEL) {
        const std::wstring currentLanguageCode = Localization::GetCurrentLanguageCode();
        for (const auto& pair : g_languageMenuCodeById) {
            const auto textIt = g_languageMenuTextById.find(pair.first);
            if (textIt == g_languageMenuTextById.end()) {
                continue;
            }
            items.push_back({
                pair.first,
                textIt->second,
                false,
                _wcsicmp(pair.second.c_str(), currentLanguageCode.c_str()) == 0,
                false
            });
        }
        return items;
    }

    if (headerItemId == ID_MENU_INPUT_MODE_LABEL) {
        items.push_back({
            ID_MENU_INPUT_MODE_PREVIOUS_WORD,
            GetMenuItemText(ID_MENU_INPUT_MODE_PREVIOUS_WORD),
            false,
            !m_scriptInputAllText,
            false
        });
        items.push_back({
            ID_MENU_INPUT_MODE_ALL_TEXT,
            GetMenuItemText(ID_MENU_INPUT_MODE_ALL_TEXT),
            false,
            m_scriptInputAllText,
            false
        });
    }
    return items;
}

SIZE Application::MeasureMorePopupWindow(const std::vector<UiRenderer::PopupMenuItem>& items) const {
    SIZE size = { MORE_POPUP_MIN_WIDTH, 2 };
    HDC hdc = GetDC(nullptr);
    for (const UiRenderer::PopupMenuItem& item : items) {
        size.cy += item.separator ? MORE_POPUP_SEPARATOR_HEIGHT : MORE_POPUP_ITEM_HEIGHT;
        if (!hdc || item.separator) {
            continue;
        }

        RECT textRect = { 0, 0, 0, 0 };
        HFONT menuFont = GetMenuFontForItem(item.id);
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, menuFont));
        DrawTextW(hdc, item.text.c_str(), -1, &textRect, DT_CALCRECT | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        const int extraWidth = item.submenu ? MORE_POPUP_ARROW_EXTRA_WIDTH : MORE_POPUP_ITEM_EXTRA_WIDTH;
        size.cx = std::max(size.cx, (textRect.right - textRect.left) + extraWidth);
    }
    if (hdc) {
        ReleaseDC(nullptr, hdc);
    }
    size.cx += MORE_POPUP_WIDTH_PADDING;
    return size;
}

int Application::HitTestMorePopupItem(HWND popupWindow, const std::vector<UiRenderer::PopupMenuItem>& items, POINT screenPoint) const {
    if (!popupWindow || !IsWindow(popupWindow)) {
        return -1;
    }

    RECT windowRect = {};
    if (!GetWindowRect(popupWindow, &windowRect) || !PtInRect(&windowRect, screenPoint)) {
        return -1;
    }

    POINT clientPoint = screenPoint;
    ScreenToClient(popupWindow, &clientPoint);
    for (size_t index = 0; index < items.size(); ++index) {
        const RECT itemRect = GetMorePopupItemRect(popupWindow, items, index);
        if (PtInRect(&itemRect, clientPoint)) {
            return items[index].separator ? -1 : static_cast<int>(index);
        }
    }
    return -1;
}

RECT Application::GetMorePopupItemRect(HWND popupWindow, const std::vector<UiRenderer::PopupMenuItem>& items, size_t index) const {
    RECT clientRect = {};
    if (!popupWindow || !IsWindow(popupWindow) || !GetClientRect(popupWindow, &clientRect) || index >= items.size()) {
        return RECT{ 0, 0, 0, 0 };
    }

    int top = 1;
    for (size_t itemIndex = 0; itemIndex < index; ++itemIndex) {
        top += items[itemIndex].separator ? MORE_POPUP_SEPARATOR_HEIGHT : MORE_POPUP_ITEM_HEIGHT;
    }
    const int itemHeight = items[index].separator ? MORE_POPUP_SEPARATOR_HEIGHT : MORE_POPUP_ITEM_HEIGHT;
    return RECT{ 1, top, clientRect.right - 1, top + itemHeight };
}

bool Application::IsPointInWindow(HWND windowHandle, POINT screenPoint) const {
    if (!windowHandle || !IsWindow(windowHandle)) {
        return false;
    }
    RECT windowRect = {};
    return GetWindowRect(windowHandle, &windowRect) && PtInRect(&windowRect, screenPoint);
}

bool Application::IsPointInMoreButton(POINT screenPoint) const {
    if (!m_hMoreButton || !IsWindow(m_hMoreButton)) {
        return false;
    }
    RECT buttonRect = {};
    return GetWindowRect(m_hMoreButton, &buttonRect) && PtInRect(&buttonRect, screenPoint);
}

LRESULT CALLBACK Application::MorePopupWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* owner = static_cast<Application*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        return TRUE;
    }

    auto* owner = reinterpret_cast<Application*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!owner) {
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return owner->HandleMorePopupMessage(hWnd, message, wParam, lParam);
}

LRESULT Application::HandleMorePopupMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const bool isMainPopup = hWnd == m_hMorePopupWindow;
    std::vector<UiRenderer::PopupMenuItem>& items = isMainPopup ? m_morePopupItems : m_moreSubPopupItems;

    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
        UpdateMorePopupTracking();
        return 0;

    case WM_LBUTTONUP:
        {
            POINT cursorPos = {};
            GetCursorPos(&cursorPos);
            const int itemIndex = HitTestMorePopupItem(hWnd, items, cursorPos);
            if (itemIndex < 0) {
                return 0;
            }

            const UiRenderer::PopupMenuItem& item = items[static_cast<size_t>(itemIndex)];
            if (item.separator) {
                return 0;
            }
            if (isMainPopup && item.submenu) {
                EnsureMoreSubPopup(item.id);
                return 0;
            }

            CloseMorePopupWindows();
            PostMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(item.id, 0), 0);
            return 0;
        }

    case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT clientRect = {};
            GetClientRect(hWnd, &clientRect);
            HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, GetStyledMenuFont()));
            UiRenderer::DrawPopupMenu(hdc, clientRect, items, isMainPopup ? m_hoveredMorePopupItemId : m_hoveredMoreSubPopupItemId);
            SelectObject(hdc, oldFont);
            EndPaint(hWnd, &ps);
            return 0;
        }

    case WM_DESTROY:
        if (isMainPopup) {
            m_hMorePopupWindow = nullptr;
            m_hoveredMorePopupItemId = 0;
            m_morePopupItems.clear();
        } else {
            m_hMoreSubPopupWindow = nullptr;
            m_hoveredMoreSubPopupItemId = 0;
            m_moreSubPopupItems.clear();
            m_activeMoreSubMenuHeaderId = 0;
        }
        return 0;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}

void Application::ApplyLocalization() {
    if (m_hHintLabel) {
        SetWindowTextW(m_hHintLabel, T(L"hint.label"));
    }
    if (m_hReloadButton) {
        SetWindowTextW(m_hReloadButton, T(L"button.reload_scripts"));
    }
    if (m_hOpenFolderButton) {
        SetWindowTextW(m_hOpenFolderButton, T(L"button.open_scripts_folder"));
    }
    if (m_hMoreButton) {
        SetWindowTextW(m_hMoreButton, T(L"button.more"));
    }

    wchar_t statusText[256] = {};
    if (m_hStatusLabel) {
        GetWindowTextW(m_hStatusLabel, statusText, static_cast<int>(_countof(statusText)));
    }
    bool shouldSetReadyStatus = statusText[0] == L'\0';
    if (!shouldSetReadyStatus) {
        const std::vector<std::wstring> languageCodes = Localization::GetAvailableLanguageCodes();
        for (const std::wstring& languageCode : languageCodes) {
            const wchar_t* readyTextForLanguage = Localization::GetTextByName(L"status.ready", languageCode);
            if (readyTextForLanguage && readyTextForLanguage[0] != L'\0'
                && wcscmp(statusText, readyTextForLanguage) == 0) {
                shouldSetReadyStatus = true;
                break;
            }
        }
    }
    if (shouldSetReadyStatus) {
        SetStatusText(T(L"status.ready"));
    }

    if (m_toolTip) {
        m_toolTip->AddTool(m_hReloadButton, T(L"tooltip.reload"));
        m_toolTip->AddTool(m_hScriptList, T(L"tooltip.script_list"));
        m_toolTip->AddTool(m_hOpenFolderButton, T(L"tooltip.open_scripts_folder"));
        m_toolTip->AddTool(m_hMoreButton, T(L"tooltip.more"));
    }

    CreateMoreMenu();
    const auto localizeInfoWindow = [](HWND infoWindow, InfoWindowKind kind) {
        if (!infoWindow || !IsWindow(infoWindow)) {
            return;
        }
        const wchar_t* title = GetInfoWindowTitleByKind(static_cast<int>(kind));
        SetWindowTextW(infoWindow, title);
        auto* state = reinterpret_cast<InfoWindowState*>(
            GetWindowLongPtrW(infoWindow, GWLP_USERDATA)
        );
        if (!state) {
            return;
        }
        state->title = title;
        SetWindowTextW(state->titleLabel, title);
        SetWindowTextW(state->closeButton, T(L"info.button.close"));
        if (state->actionButton) {
            SetWindowTextW(state->actionButton, T(L"info.button.check_updates"));
        }
        if (state->fullscreenCheckbox) {
            SetWindowTextW(
                state->fullscreenCheckbox,
                T(L"application_blacklist.disable_fullscreen")
            );
        }
        if (state->blacklistList) {
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT;
            column.pszText = const_cast<wchar_t*>(T(L"application_blacklist.column.application"));
            ListView_SetColumn(state->blacklistList, 0, &column);
            column.pszText = const_cast<wchar_t*>(T(L"application_blacklist.column.path"));
            ListView_SetColumn(state->blacklistList, 1, &column);
        }
        if (state->runningPickerButton) {
            SetWindowTextW(state->runningPickerButton, T(L"application_blacklist.running"));
        }
        if (state->exePickerButton) {
            SetWindowTextW(state->exePickerButton, T(L"application_blacklist.add_exe"));
        }
        if (state->removeButton) {
            SetWindowTextW(state->removeButton, T(L"application_blacklist.remove"));
        }
    };
    localizeInfoWindow(m_hAboutWindow, InfoWindowKind::About);
    localizeInfoWindow(m_hLogsWindow, InfoWindowKind::Logs);
    localizeInfoWindow(m_hApplicationBlacklistWindow, InfoWindowKind::ApplicationBlacklist);
    if (m_hAboutWindow && IsWindow(m_hAboutWindow)) {
        UpdateInfoWindowText(InfoWindowKind::About, BuildAboutText());
    }
    if (m_hLogsWindow && IsWindow(m_hLogsWindow)) {
        UpdateInfoWindowText(InfoWindowKind::Logs, L"");
    }
}

void Application::SetLanguage(const std::wstring& languageCode) {
    const std::wstring oldLanguageCode = Localization::GetCurrentLanguageCode();
    Localization::SetCurrentLanguageCode(languageCode);
    if (_wcsicmp(oldLanguageCode.c_str(), Localization::GetCurrentLanguageCode().c_str()) == 0) {
        return;
    }
    SaveLanguageSetting(GetLanguageSettingsPath(GetExecutableDirectory()));
    ApplyLocalization();
    RefreshScriptList();
    SetStatusText(T(L"status.language_updated"));
}

void Application::UpdateScriptInputModeMenuChecks() {
    if (m_activeMoreSubMenuHeaderId == ID_MENU_INPUT_MODE_LABEL && m_hMoreSubPopupWindow && IsWindow(m_hMoreSubPopupWindow)) {
        m_moreSubPopupItems = BuildSubMorePopupItems(ID_MENU_INPUT_MODE_LABEL);
        InvalidateRect(m_hMoreSubPopupWindow, nullptr, FALSE);
    }
}

void Application::SetScriptInputMode(bool allTextInputMode) {
    if (m_scriptInputAllText == allTextInputMode) {
        return;
    }
    m_scriptInputAllText = allTextInputMode;
    SaveScriptInputModeSetting(GetLanguageSettingsPath(GetExecutableDirectory()), m_scriptInputAllText);
    UpdateScriptInputModeMenuChecks();
    SetStatusText(allTextInputMode ? T(L"app.status.input_mode_all_text") : T(L"app.status.input_mode_previous_word"));
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
}

void Application::RestoreFromTray() {
    if (!m_hWnd || !IsWindow(m_hWnd)) {
        return;
    }
    ShowWindow(m_hWnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_hWnd);
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
    AppendLog(std::wstring(T(L"app.log.scripts.reload_prefix")) + m_scriptsDirectory + L".");

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
        AppendLog(std::wstring(T(L"app.log.scripts.warning_prefix")) + loadResult.warning);
        OutputDebugStringW(loadResult.warning.c_str());
    }

    if (!announceResult) {
        return;
    }
    if (m_scripts.empty()) {
        SetStatusText(T(L"status.no_scripts_found"));
        AppendLog(T(L"app.log.scripts.none"));
        return;
    }

    size_t registeredCount = 0;
    for (const auto& script : m_scripts) {
        if (script.hotkeyRegistered) {
            ++registeredCount;
        }
    }

    const std::wstring status = std::wstring(T(L"app.status.scripts_count_prefix"))
        + std::to_wstring(m_scripts.size())
        + T(L"app.status.hotkeys_active_prefix")
        + std::to_wstring(registeredCount)
        + L".";
    SetStatusText(status);
    AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
}

void Application::RegisterHotkeys() {
    ClearTrackedHotkeys();
    ClearHookHotkeys();
    std::vector<TrackedHotkey> assignedHotkeys;
    for (auto& script : m_scripts) {
        script.hotkeyRegistered = false;
        script.hotkeyError.clear();
        if (!script.manifest.enabled) {
            script.hotkeyError = T(L"app.status.disabled_by_user");
            continue;
        }
        if (script.manifest.virtualKey == 0) {
            script.hotkeyError = T(L"app.status.invalid_hotkey");
            continue;
        }

        const TrackedHotkey currentHotkey{
            script.manifest.modifiers & HOTKEY_MODIFIER_MASK,
            script.manifest.virtualKey
        };
        const auto duplicateIt = std::find_if(assignedHotkeys.begin(), assignedHotkeys.end(),
            [&currentHotkey](const TrackedHotkey& assigned) {
                return assigned.modifiers == currentHotkey.modifiers
                    && assigned.virtualKey == currentHotkey.virtualKey;
            });
        if (duplicateIt != assignedHotkeys.end()) {
            script.hotkeyError = T(L"app.status.hotkey_taken");
            AppendLog(std::wstring(T(L"app.log.hotkey.error_prefix")) + script.manifest.name + L" [" + script.manifest.hotkeyText + L"]: "
                + script.hotkeyError);
            continue;
        }

        AddHookHotkey(script.hotkeyId, currentHotkey.modifiers, currentHotkey.virtualKey);
        script.hotkeyRegistered = true;
        m_scriptIndexByHotkeyId[script.hotkeyId] = &script - m_scripts.data();
        AddTrackedHotkey(currentHotkey.modifiers, currentHotkey.virtualKey);
        assignedHotkeys.push_back(currentHotkey);
    }
}

void Application::UnregisterHotkeys() {
    m_scriptIndexByHotkeyId.clear();
    ClearTrackedHotkeys();
    ClearHookHotkeys();
}

void Application::RefreshScriptList() {
    SendMessageW(m_hScriptList, LB_RESETCONTENT, 0, 0);
    for (const auto& script : m_scripts) {
        std::wstring line = script.manifest.enabled ? T(L"app.script_list.enabled_prefix") : T(L"app.script_list.disabled_prefix");
        line += script.manifest.name + L" [" + script.manifest.hotkeyText + L"]";
        if (!script.manifest.description.empty()) {
            line += L" - " + script.manifest.description;
        }
        if (script.manifest.enabled && !script.hotkeyRegistered) {
            line += T(L"script_list.hotkey_unavailable_prefix");
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
        SetStatusText(T(L"app.status.select_scripts"));
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
            AppendLog(std::wstring(T(L"app.log.scripts.enable_update_failed_prefix"))
                + script.manifest.name + L"\": " + updateError);
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

    const std::wstring actionText = enabled ? T(L"app.status.enabled_word") : T(L"app.status.disabled_word");
    const std::wstring status = std::wstring(T(L"app.status.scripts_word_prefix")) + actionText + L": " + std::to_wstring(updatedCount)
        + T(L"app.status.errors_prefix") + std::to_wstring(failedCount) + L".";
    SetStatusText(status);
    AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
}

void Application::AddScriptViaDialog() {
    wchar_t filePath[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    const std::wstring filter = BuildDialogFilter({
        { L"app.dialog.filter.scripts", L"*.tmscript" },
        { L"app.dialog.filter.all_files", L"*.*" }
    });
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) {
        const DWORD dialogError = CommDlgExtendedError();
        if (dialogError != 0) {
            const std::wstring error = std::wstring(T(L"app.dialog.file_dialog_error_prefix")) + std::to_wstring(dialogError);
            SetStatusText(error);
            AppendLog(std::wstring(T(L"app.log.scripts.error_prefix")) + error);
        }
        return;
    }

    ImportScriptFiles({ filePath });
}

void Application::RemoveSelectedScripts() {
    const std::vector<size_t> selectedIndices = GetSelectedScriptIndices();
    if (selectedIndices.empty()) {
        SetStatusText(T(L"app.status.select_scripts"));
        return;
    }

    std::wstring prompt = std::wstring(T(L"app.status.delete_selected_prefix"))
        + std::to_wstring(selectedIndices.size()) + T(L"app.status.delete_selected_suffix");
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
    const int answer = ShowStyledMessageDialog(T(L"app.title.delete_scripts"), prompt, T(L"app.button.delete"), T(L"app.button.cancel"));
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
            AppendLog(std::wstring(T(L"app.log.scripts.delete_failed_prefix"))
                + path + T(L"app.log.scripts.delete_failed_code_prefix") + std::to_wstring(removeError.value()));
            continue;
        }
        ++removedCount;
    }

    if (removedCount > 0) {
        ReloadScripts(false);
    }

    const std::wstring status = std::wstring(T(L"app.status.deleted_prefix")) + std::to_wstring(removedCount)
        + T(L"app.status.errors_prefix") + std::to_wstring(failedCount) + L".";
    SetStatusText(status);
    AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
    if (failedCount > 0 && !firstErrorPath.empty()) {
        ShowStyledMessage(T(L"app.title.delete_error"), std::wstring(T(L"app.status.not_all_deleted")) + firstErrorPath);
    }
}

void Application::ImportScriptsFromZip() {
    if (m_archiveTaskInProgress) {
        SetStatusText(T(L"app.status.archive_busy"));
        return;
    }

    wchar_t archivePath[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    const std::wstring filter = BuildDialogFilter({
        { L"app.dialog.filter.zip_archives", L"*.zip" },
        { L"app.dialog.filter.all_files", L"*.*" }
    });
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = archivePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    m_archiveTaskInProgress = true;
    const std::wstring archivePathString = archivePath;
    const std::wstring scriptsDirectory = m_scriptsDirectory;
    const ScriptRunner scriptRunner = m_scriptRunner;
    const HWND windowHandle = m_hWnd;
    std::thread([archivePathString, scriptsDirectory, scriptRunner, windowHandle]() {
        auto* result = new ImportZipTaskResult();

        wchar_t tempDirectory[MAX_PATH] = {};
        if (!GetTempPathW(MAX_PATH, tempDirectory)) {
            result->success = false;
            result->errorMessage = T(L"app.error.tmp_dir_path");
            PostOwnedMessage(windowHandle, WM_IMPORT_ZIP_COMPLETE, result);
            return;
        }

        wchar_t tempName[MAX_PATH] = {};
        if (!GetTempFileNameW(tempDirectory, L"tmz", 0, tempName)) {
            result->success = false;
            result->errorMessage = T(L"app.error.tmp_path_create");
            PostOwnedMessage(windowHandle, WM_IMPORT_ZIP_COMPLETE, result);
            return;
        }
        DeleteFileW(tempName);

        if (!CreateDirectoryW(tempName, nullptr)) {
            result->success = false;
            result->errorMessage = T(L"app.error.tmp_folder_create");
            PostOwnedMessage(windowHandle, WM_IMPORT_ZIP_COMPLETE, result);
            return;
        }

        const std::wstring escapedArchive = PowerShellUtils::EscapeSingleQuoted(archivePathString);
        const std::wstring escapedTempDir = PowerShellUtils::EscapeSingleQuoted(tempName);
        const std::wstring extractScript =
            L"$ErrorActionPreference='Stop'\n"
            L"$archivePath='" + escapedArchive + L"'\n"
            L"$destinationPath='" + escapedTempDir + L"'\n"
            L"Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath -Force\n";

        std::wstring ignoredOutput;
        std::wstring executeError;
        if (!scriptRunner.ExecutePowerShellScript(extractScript, L"", &ignoredOutput, &executeError)) {
            std::error_code cleanupError;
            fs::remove_all(fs::path(tempName), cleanupError);
            result->success = false;
            result->errorMessage = std::wstring(T(L"app.error.zip_extract_prefix")) + executeError;
            PostOwnedMessage(windowHandle, WM_IMPORT_ZIP_COMPLETE, result);
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

        for (const auto& filePath : scriptFiles) {
            std::wstring copiedPath;
            if (ImportScriptFileToDirectory(scriptsDirectory, filePath, &copiedPath)) {
                ++result->importedCount;
                result->importedPaths.push_back(copiedPath);
            } else {
                ++result->skippedCount;
            }
        }

        std::error_code cleanupError;
        fs::remove_all(fs::path(tempName), cleanupError);
        result->success = true;
        PostOwnedMessage(windowHandle, WM_IMPORT_ZIP_COMPLETE, result);
    }).detach();
}

void Application::ExportScriptsToZip() {
    if (m_archiveTaskInProgress) {
        SetStatusText(T(L"app.status.archive_busy"));
        return;
    }

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
        SetStatusText(T(L"app.status.no_scripts_export"));
        return;
    }

    wchar_t archivePath[MAX_PATH] = {};
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t defaultName[128] = {};
    swprintf_s(defaultName, TM_APP_NAME_W L"-scripts-%04u%02u%02u-%02u%02u%02u.zip",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wcscpy_s(archivePath, defaultName);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    const std::wstring filter = BuildDialogFilter({
        { L"app.dialog.filter.zip_archives", L"*.zip" },
        { L"app.dialog.filter.all_files", L"*.*" }
    });
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrDefExt = L"zip";
    ofn.lpstrFile = archivePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    m_archiveTaskInProgress = true;
    const std::wstring archivePathString = archivePath;
    const std::wstring scriptsDirectory = m_scriptsDirectory;
    const ScriptRunner scriptRunner = m_scriptRunner;
    const HWND windowHandle = m_hWnd;
    std::thread([archivePathString, scriptsDirectory, scriptFileCount, scriptRunner, windowHandle]() {
        auto* result = new ExportZipTaskResult();
        result->archivePath = archivePathString;
        result->scriptFileCount = scriptFileCount;

        const std::wstring escapedScriptsDir = PowerShellUtils::EscapeSingleQuoted(scriptsDirectory);
        const std::wstring escapedArchive = PowerShellUtils::EscapeSingleQuoted(archivePathString);
        const std::wstring exportScript =
            L"$ErrorActionPreference='Stop'\n"
            L"$scriptsDir='" + escapedScriptsDir + L"'\n"
            L"$destinationPath='" + escapedArchive + L"'\n"
            L"$files=Get-ChildItem -LiteralPath $scriptsDir -Filter '*.tmscript' -File\n"
            + std::wstring(L"if(-not $files){ throw '")
            + T(L"app.error.no_scripts_to_export_ps")
            + L"' }\n"
            L"Compress-Archive -LiteralPath $files.FullName -DestinationPath $destinationPath -Force\n";

        std::wstring ignoredOutput;
        result->success = scriptRunner.ExecutePowerShellScript(exportScript, L"", &ignoredOutput, &result->errorMessage);
        PostOwnedMessage(windowHandle, WM_EXPORT_ZIP_COMPLETE, result);
    }).detach();
}

void Application::ImportScriptFiles(const std::vector<std::wstring>& filePaths) {
    if (filePaths.empty()) {
        return;
    }

    int importedCount = 0;
    int skippedCount = 0;
    for (const auto& filePath : filePaths) {
        std::wstring copiedPath;
        if (ImportScriptFileToDirectory(m_scriptsDirectory, filePath, &copiedPath)) {
            ++importedCount;
            AppendLog(std::wstring(T(L"app.log.scripts.imported_prefix")) + copiedPath);
        } else {
            ++skippedCount;
        }
    }

    if (importedCount > 0) {
        ReloadScripts(false);
        const std::wstring status =
            std::wstring(T(L"app.status.scripts_added_prefix")) + std::to_wstring(importedCount)
            + T(L"app.status.skipped_prefix") + std::to_wstring(skippedCount) + L".";
        SetStatusText(status);
        AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
        return;
    }

    const std::wstring status = T(L"app.status.no_suitable_add");
    SetStatusText(status);
    AppendLog(std::wstring(T(L"app.log.scripts.prefix")) + status);
}

void Application::ExecuteSelectedScript() {
    size_t selectedIndex = 0;
    if (!GetPrimarySelectedScriptIndex(&selectedIndex)) {
        SetStatusText(T(L"app.status.select_script"));
        return;
    }
    if (!m_scripts[selectedIndex].manifest.enabled) {
        SetStatusText(T(L"app.status.script_disabled"));
        return;
    }
    ExecuteScript(m_scripts[selectedIndex], true, false);
}

void Application::ExecuteScriptByHotkeyId(int hotkeyId) {
    const auto it = m_scriptIndexByHotkeyId.find(hotkeyId);
    if (it == m_scriptIndexByHotkeyId.end()) {
        m_scriptExecutionGate.Release(GetTickCount64());
        return;
    }
    if (it->second >= m_scripts.size()) {
        m_scriptExecutionGate.Release(GetTickCount64());
        return;
    }
    if (!m_scripts[it->second].manifest.enabled) {
        m_scriptExecutionGate.Release(GetTickCount64());
        return;
    }
    ExecuteScript(m_scripts[it->second], false, true);
}

void Application::ExecuteScript(const RegisteredScript& script, bool clipboardOnly, bool reservationHeld) {
    if (!reservationHeld && !m_scriptExecutionGate.TryReserve(GetTickCount64())) {
        SetStatusText(T(L"app.status.script_already_running"));
        return;
    }

    SetStatusText(std::wstring(T(L"app.status.running_prefix")) + script.manifest.name);
    AppendLog(std::wstring(T(L"app.log.script.start_prefix")) + script.manifest.name + L"\".");
    if (!script.manifest.scriptBody.empty()) {
        AppendLog(T(L"app.log.script.launch_mode_inline"));
    } else {
        AppendLog(std::wstring(T(L"app.log.script.command_prefix")) + script.manifest.commandLine);
    }

    const std::wstring scriptName = script.manifest.name;
    const std::wstring scriptBody = script.manifest.scriptBody;
    const std::wstring commandLine = script.manifest.commandLine;
    const bool allTextInputMode = m_scriptInputAllText;
    const bool useClipboardOnly = clipboardOnly;
    const TextBridge textBridge = m_textBridge;
    const ScriptRunner scriptRunner = m_scriptRunner;
    const HWND windowHandle = m_hWnd;
    const std::wstring noTextAvailableMessage = T(L"app.status.no_text_available");
    const std::wstring workerExceptionPrefix =
        std::wstring(T(L"app.error.script_worker_exception_prefix")) + L" ";
    const std::wstring unknownWorkerException = T(L"app.error.script_worker_unknown_exception");
    const HWND inputTargetWindow = useClipboardOnly ? nullptr : GetForegroundWindow();
    const InputBuffer::ContextId inputContext =
        reinterpret_cast<InputBuffer::ContextId>(inputTargetWindow);
    InputBuffer::PreviousWordCapture inputCapture;
    const bool hasInputCapture = inputTargetWindow
        && !useClipboardOnly
        && (allTextInputMode
            ? PeekAllTextFromInputBuffer(inputContext, &inputCapture)
            : PeekPreviousWordFromInputBuffer(inputContext, &inputCapture))
        && !inputCapture.word.empty();

    try {
        std::thread worker([scriptName,
                            scriptBody,
                            commandLine,
                            allTextInputMode,
                            useClipboardOnly,
                            textBridge,
                            scriptRunner,
                            windowHandle,
                            noTextAvailableMessage,
                            workerExceptionPrefix,
                            unknownWorkerException,
                            inputTargetWindow,
                            hasInputCapture,
                            inputCapture]() {
            try {
                auto result = std::make_unique<ScriptExecutionTaskResult>();
                result->scriptName = scriptName;
                result->clipboardMode = useClipboardOnly;
                result->inputTargetWindow = hasInputCapture ? inputTargetWindow : nullptr;

                try {
                    std::wstring selectedText;
                    bool hasSelection = false;
                    bool inputBufferMode = false;
                    std::wstring sourceText;

                    if (!useClipboardOnly && !hasInputCapture) {
                        selectedText = textBridge.GetSelectedText();
                        hasSelection = !selectedText.empty();
                    }

                    switch (ScriptInputSource::Choose(
                        useClipboardOnly,
                        hasSelection,
                        hasInputCapture
                    )) {
                    case ScriptInputSource::Type::Clipboard:
                        ClipboardUtils::ReadText(windowHandle, &sourceText);
                        break;
                    case ScriptInputSource::Type::Selection:
                        sourceText = selectedText;
                        break;
                    case ScriptInputSource::Type::TrackedInput:
                        sourceText = inputCapture.word;
                        inputBufferMode = true;
                        break;
                    case ScriptInputSource::Type::None:
                        break;
                    }

                    result->sourceText = sourceText;
                    result->hasSelection = hasSelection;
                    result->inputBufferMode = inputBufferMode;
                    result->allTextInputMode = allTextInputMode;
                    result->inputCapture = inputCapture;

                    if (sourceText.empty()) {
                        result->noTextAvailable = true;
                        result->executeOk = false;
                        result->executionError = noTextAvailableMessage;
                    } else if (!scriptBody.empty()) {
                        result->executeOk = scriptRunner.ExecutePowerShellScript(
                            scriptBody,
                            sourceText,
                            &result->outputText,
                            &result->executionError
                        );
                    } else {
                        result->executeOk = scriptRunner.Execute(
                            commandLine,
                            sourceText,
                            &result->outputText,
                            &result->executionError
                        );
                    }
                } catch (const std::exception& error) {
                    result->executeOk = false;
                    result->executionError = workerExceptionPrefix
                        + EncodingUtils::Utf8ToWide(error.what());
                } catch (...) {
                    result->executeOk = false;
                    result->executionError = unknownWorkerException;
                }

                result->inputReady = TextBridgeInputUtils::WaitForInputReady(
                    result->executeOk && result->inputBufferMode,
                    [&]() {
                        return textBridge.WaitForModifiersRelease(inputTargetWindow);
                    }
                );

                SendScriptExecutionCompletion(windowHandle, std::move(result));
            } catch (...) {
                SendScriptExecutionCompletion(
                    windowHandle,
                    std::unique_ptr<ScriptExecutionTaskResult>()
                );
            }
        });
        worker.detach();
    } catch (const std::system_error&) {
        m_scriptExecutionGate.Release(GetTickCount64());
        const std::wstring message = T(L"app.status.script_thread_start_failed");
        SetStatusText(message);
        AppendLog(std::wstring(T(L"app.log.script.error_prefix2")) + message);
    }
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
        return std::wstring(L".\\") + TM_APP_NAME_W + L".exe";
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

bool Application::RegisterMorePopupWindowClass() {
    if (m_morePopupWindowClassRegistered) {
        return true;
    }

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MorePopupWindowProc;
    wcex.hInstance = m_hInstance;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = m_hCardBrush;
    wcex.lpszClassName = MORE_POPUP_WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_morePopupWindowClassRegistered = true;
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
        L""
    );
}

void Application::ShowApplicationBlacklistWindow() {
    CreateOrActivateInfoWindow(
        InfoWindowKind::ApplicationBlacklist,
        m_hApplicationBlacklistWindow,
        GetInfoWindowTitleByKind(static_cast<int>(InfoWindowKind::ApplicationBlacklist)),
        L""
    );
}

void Application::CreateOrActivateInfoWindow(InfoWindowKind kind, HWND& targetHandle, const wchar_t* title, const std::wstring& bodyText) {
    if (targetHandle && IsWindow(targetHandle)) {
        if (kind != InfoWindowKind::Logs) {
            UpdateInfoWindowText(kind, bodyText);
        }
        ShowWindow(targetHandle, SW_SHOWNORMAL);
        SetForegroundWindow(targetHandle);
        if (kind == InfoWindowKind::ApplicationBlacklist) {
            SetFocus(GetDlgItem(targetHandle, ID_INFO_FULLSCREEN_CHECKBOX));
        }
        return;
    }

    InfoWindowState* state = new InfoWindowState();
    state->owner = this;
    state->kind = static_cast<int>(kind);
    state->title = title ? title : L"";
    if (kind != InfoWindowKind::Logs) {
        state->text = bodyText;
    }
    state->editBrush = CreateSolidBrush(RGB(45, 45, 45));

    RECT ownerRect = {};
    GetWindowRect(m_hWnd, &ownerRect);
    const bool isLogsWindow = kind == InfoWindowKind::Logs;
    const bool isApplicationBlacklistWindow = kind == InfoWindowKind::ApplicationBlacklist;
    const int width = isApplicationBlacklistWindow ? 760 : (isLogsWindow ? 700 : 560);
    const int height = isApplicationBlacklistWindow ? 520 : (isLogsWindow ? 480 : 360);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    const DWORD infoStyle = isLogsWindow
        ? WS_OVERLAPPEDWINDOW
        : (isApplicationBlacklistWindow
            ? WS_OVERLAPPEDWINDOW
            : (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX));

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
    if (kind == InfoWindowKind::ApplicationBlacklist) {
        SetFocus(state->fullscreenCheckbox);
    }
}

void Application::OnInfoWindowClosed(InfoWindowKind kind) {
    if (kind == InfoWindowKind::About) {
        m_hAboutWindow = nullptr;
    } else if (kind == InfoWindowKind::Logs) {
        m_hLogsWindow = nullptr;
    } else if (kind == InfoWindowKind::ApplicationBlacklist) {
        m_hApplicationBlacklistWindow = nullptr;
    }
}

void Application::UpdateInfoWindowText(InfoWindowKind kind, const std::wstring& text) {
    HWND target = nullptr;
    if (kind == InfoWindowKind::About) {
        target = m_hAboutWindow;
    } else if (kind == InfoWindowKind::Logs) {
        target = m_hLogsWindow;
    } else if (kind == InfoWindowKind::ApplicationBlacklist) {
        target = m_hApplicationBlacklistWindow;
    }
    if (!target || !IsWindow(target)) {
        return;
    }

    auto* state = reinterpret_cast<InfoWindowState*>(GetWindowLongPtrW(target, GWLP_USERDATA));
    if (!state || !state->textControl) {
        return;
    }
    if (kind == InfoWindowKind::Logs) {
        const std::wstring logText = LogFile::Read(m_logPath);
        state->logPlaceholderVisible = logText.empty();
        SetWindowTextW(
            state->textControl,
            state->logPlaceholderVisible ? T(L"log.is_empty") : logText.c_str()
        );
        return;
    }
    state->text = text;
    SetWindowTextW(state->textControl, state->text.c_str());
}

void Application::RefreshApplicationBlacklistList() {
    if (!m_hApplicationBlacklistWindow || !IsWindow(m_hApplicationBlacklistWindow)) {
        return;
    }
    auto* state = reinterpret_cast<InfoWindowState*>(
        GetWindowLongPtrW(m_hApplicationBlacklistWindow, GWLP_USERDATA)
    );
    if (!state || !state->blacklistList) {
        return;
    }

    ListView_DeleteAllItems(state->blacklistList);
    int index = 0;
    for (const std::wstring& path : m_applicationBlacklist.Paths()) {
        std::wstring executableName = fs::path(path).filename().wstring();
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.pszText = executableName.data();
        const int inserted = ListView_InsertItem(state->blacklistList, &item);
        if (inserted >= 0) {
            ListView_SetItemText(
                state->blacklistList,
                inserted,
                1,
                const_cast<wchar_t*>(path.c_str())
            );
            ++index;
        }
    }
    EnableWindow(state->removeButton, FALSE);
}

bool Application::PublishApplicationBlacklist(ApplicationBlacklist updated) {
    std::wstring error;
    if (!updated.Save(m_blacklistPath, &error)) {
        std::wstring message = T(L"application_blacklist.save_error");
        if (!error.empty()) {
            message += L"\r\n\r\n" + error;
        }
        ShowStyledMessage(T(L"application_blacklist.title"), message);
        return false;
    }

    m_applicationBlacklist = std::move(updated);
    InvalidateForegroundBlockCache();
    RefreshApplicationBlacklistList();
    return true;
}

void Application::AddApplicationsToBlacklist(const std::vector<std::wstring>& paths) {
    ApplicationBlacklist updated = m_applicationBlacklist;
    bool changed = false;
    for (const std::wstring& path : paths) {
        changed = updated.Add(path) || changed;
    }
    if (changed) {
        PublishApplicationBlacklist(std::move(updated));
    }
}

void Application::RemoveSelectedApplicationFromBlacklist() {
    if (!m_hApplicationBlacklistWindow || !IsWindow(m_hApplicationBlacklistWindow)) {
        return;
    }
    auto* state = reinterpret_cast<InfoWindowState*>(
        GetWindowLongPtrW(m_hApplicationBlacklistWindow, GWLP_USERDATA)
    );
    if (!state || !state->blacklistList) {
        return;
    }

    const int selected = ListView_GetNextItem(state->blacklistList, -1, LVNI_SELECTED);
    if (selected < 0) {
        return;
    }
    std::wstring path(32768, L'\0');
    LVITEMW item = {};
    item.iSubItem = 1;
    item.pszText = path.data();
    item.cchTextMax = static_cast<int>(path.size());
    const int length = static_cast<int>(SendMessageW(
        state->blacklistList,
        LVM_GETITEMTEXTW,
        selected,
        reinterpret_cast<LPARAM>(&item)
    ));
    path.resize(static_cast<size_t>(std::max(0, length)));

    ApplicationBlacklist updated = m_applicationBlacklist;
    if (updated.Remove(path)) {
        PublishApplicationBlacklist(std::move(updated));
    }
}

std::vector<std::wstring> Application::SelectRunningApplications() {
    std::vector<RunningApplication> applications = EnumerateVisibleRunningApplications();
    std::vector<std::wstring> selectedPaths;
    MessageWindowState* state = new MessageWindowState();
    state->owner = this;
    state->title = T(L"application_blacklist.running");
    state->primaryButtonText = T(L"application_blacklist.add_selected");
    state->secondaryButtonText = T(L"app.button.cancel");
    state->hasSecondaryButton = true;
    state->usesListBox = true;
    state->runningApplicationSelection = true;
    state->runningApplications = std::move(applications);
    state->selectedApplicationPathsOut = &selectedPaths;
    state->editBrush = CreateSolidBrush(RGB(45, 45, 45));

    RECT ownerRect = {};
    HWND dialogOwner = m_hApplicationBlacklistWindow;
    GetWindowRect(dialogOwner, &ownerRect);
    const int width = 760;
    const int height = 520;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    HWND messageWindow = CreateWindowExW(
        0,
        MESSAGE_WINDOW_CLASS_NAME,
        state->title.c_str(),
        WS_OVERLAPPEDWINDOW,
        x, y, width, height,
        dialogOwner,
        nullptr,
        m_hInstance,
        state
    );
    if (!messageWindow) {
        DeleteObject(state->editBrush);
        delete state;
        return selectedPaths;
    }

    EnableWindow(dialogOwner, FALSE);
    ShowWindow(messageWindow, SW_SHOWNORMAL);
    UpdateWindow(messageWindow);
    MSG message = {};
    while (IsWindow(messageWindow) && GetMessageW(&message, nullptr, 0, 0)) {
        if (!IsDialogMessageW(messageWindow, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(dialogOwner, TRUE);
    SetForegroundWindow(dialogOwner);
    return selectedPaths;
}

std::vector<std::wstring> Application::SelectExecutableApplications() {
    std::vector<std::wstring> paths;
    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)
    );
    if (SUCCEEDED(result) && !dialog) {
        result = E_UNEXPECTED;
    }
    if (SUCCEEDED(result)) {
        DWORD options = 0;
        result = dialog->GetOptions(&options);
        if (SUCCEEDED(result)) {
            result = dialog->SetOptions(
                options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT
            );
        }
        const COMDLG_FILTERSPEC filter = { L"Applications (*.exe)", L"*.exe" };
        if (SUCCEEDED(result)) {
            result = dialog->SetFileTypes(1, &filter);
        }
        if (SUCCEEDED(result)) {
            result = dialog->Show(m_hApplicationBlacklistWindow);
        }
    }

    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        if (dialog) {
            dialog->Release();
        }
        return paths;
    }

    IShellItemArray* items = nullptr;
    if (SUCCEEDED(result)) {
        result = dialog->GetResults(&items);
    }
    if (SUCCEEDED(result) && !items) {
        result = E_UNEXPECTED;
    }
    DWORD count = 0;
    if (SUCCEEDED(result)) {
        result = items->GetCount(&count);
    }
    for (DWORD index = 0; SUCCEEDED(result) && index < count; ++index) {
        IShellItem* item = nullptr;
        result = items->GetItemAt(index, &item);
        if (FAILED(result) || !item) {
            if (item) {
                item->Release();
            } else if (SUCCEEDED(result)) {
                result = E_UNEXPECTED;
            }
            break;
        }
        PWSTR path = nullptr;
        result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        if (SUCCEEDED(result) && path) {
            paths.emplace_back(path);
        }
        CoTaskMemFree(path);
        item->Release();
    }
    if (items) {
        items->Release();
    }
    if (dialog) {
        dialog->Release();
    }
    if (FAILED(result)) {
        paths.clear();
        ShowStyledMessage(
            T(L"application_blacklist.title"),
            std::wstring(T(L"app.dialog.file_dialog_error_prefix"))
                + std::to_wstring(static_cast<long>(result))
        );
    }
    return paths;
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
    state->title = title ? title : T(L"app.title.message");
    state->text = bodyText;
    state->primaryButtonText = primaryButtonText ? primaryButtonText : T(L"app.button.ok");
    state->secondaryButtonText = secondaryButtonText ? secondaryButtonText : L"";
    state->hasSecondaryButton = secondaryButtonText != nullptr;
    state->useMonoFont = state->title.find(L"Ошибка") != std::wstring::npos
        || state->title.find(L"Error") != std::wstring::npos
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
        title ? title : T(L"app.title.message"),
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
    ShowStyledMessageDialog(title.c_str(), message, T(L"app.button.ok"), nullptr);
}

void Application::CheckForUpdates() {
    if (m_updateInProgress) {
        SetStatusText(T(L"app.status.update_already_running"));
        return;
    }

    AppendLog(T(L"app.log.update.checking"));
    m_updateInProgress = true;
    const UpdateService updateService = m_updateService;
    const HWND windowHandle = m_hWnd;
    std::thread([updateService, windowHandle]() {
        auto* result = new UpdateCheckTaskResult();
        result->check = updateService.CheckForUpdates(APP_VERSION);
        PostOwnedMessage(windowHandle, WM_UPDATE_CHECK_COMPLETE, result);
    }).detach();
}

void Application::AppendLog(const std::wstring& line) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    wchar_t prefix[32] = {};
    swprintf_s(prefix, L"[%02u:%02u:%02u] ", st.wHour, st.wMinute, st.wSecond);
    const std::wstring persistedLine = std::wstring(prefix) + line;
    if (!LogFile::Append(m_logPath, persistedLine)) {
        return;
    }

    if (!m_hLogsWindow || !IsWindow(m_hLogsWindow)) {
        return;
    }
    auto* state = reinterpret_cast<InfoWindowState*>(
        GetWindowLongPtrW(m_hLogsWindow, GWLP_USERDATA));
    if (!state || !state->textControl) {
        return;
    }

    if (state->logPlaceholderVisible) {
        SetWindowTextW(state->textControl, persistedLine.c_str());
        state->logPlaceholderVisible = false;
    } else {
        const std::wstring addition = L"\r\n" + persistedLine;
        SendMessageW(state->textControl, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(
            state->textControl,
            EM_REPLACESEL,
            FALSE,
            reinterpret_cast<LPARAM>(addition.c_str())
        );
    }
    if (!state->richEdit) {
        RedrawWindow(
            state->textControl,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW
        );
    }
    SendMessageW(state->textControl, EM_SCROLLCARET, 0, 0);
}

void Application::ClearLogs() {
    if (!LogFile::Clear(m_logPath)) {
        return;
    }
    UpdateInfoWindowText(InfoWindowKind::Logs, L"");
    SetStatusText(T(L"status.logs_cleared"));
}

std::wstring Application::BuildAboutText() const {
    std::wostringstream stream;
    stream << WINDOW_TITLE << L" " << APP_VERSION << L"\r\n\r\n";
    stream << T(L"about.loaded_scripts_prefix") << m_scripts.size() << L"\r\n";
    stream << T(L"about.scripts_directory_prefix") << m_scriptsDirectory << L"\r\n\r\n";
    stream << T(L"about.check_updates_hint");
    return stream.str();
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
            const bool usesLargeMinimum = state->kind == static_cast<int>(Application::InfoWindowKind::Logs)
                || state->kind == static_cast<int>(Application::InfoWindowKind::ApplicationBlacklist);
            info->ptMinTrackSize.x = usesLargeMinimum ? LOGS_MIN_WIDTH : INFO_MIN_WIDTH;
            info->ptMinTrackSize.y = usesLargeMinimum ? LOGS_MIN_HEIGHT : INFO_MIN_HEIGHT;
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
                const std::wstring logText = LogFile::Read(state->owner->m_logPath);
                state->logPlaceholderVisible = logText.empty();
                const wchar_t* initialText = state->logPlaceholderVisible ? T(L"log.is_empty") : logText.c_str();
                const DWORD logStyles = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
                    | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL;

                if (state->owner->m_msfteditModule) {
                    state->textControl = CreateWindowExW(
                        0, MSFTEDIT_CLASS, initialText, logStyles,
                        0, 0, 100, 100,
                        hWnd, reinterpret_cast<HMENU>(ID_INFO_TEXT), GetModuleHandleW(nullptr), nullptr
                    );
                    if (state->textControl) {
                        state->richEdit = true;
                    }
                }
                if (!state->textControl) {
                    state->textControl = CreateWindowExW(
                        0, L"EDIT", initialText, logStyles,
                        0, 0, 100, 100,
                        hWnd, reinterpret_cast<HMENU>(ID_INFO_TEXT), GetModuleHandleW(nullptr), nullptr
                    );
                    if (state->textControl) {
                        SetWindowTheme(state->textControl, L"", L"");
                    }
                }
                SendMessageW(state->textControl, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
                SendMessageW(
                    state->textControl,
                    EM_SETMARGINS,
                    EC_LEFTMARGIN | EC_RIGHTMARGIN,
                    MAKELPARAM(LIST_TEXT_PADDING, LIST_TEXT_PADDING)
                );
            } else if (state->kind == static_cast<int>(Application::InfoWindowKind::ApplicationBlacklist)) {
                state->fullscreenCheckbox = CreateWindowExW(
                    0, L"BUTTON", T(L"application_blacklist.disable_fullscreen"),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    0, 0, 100, 28,
                    hWnd,
                    reinterpret_cast<HMENU>(ID_INFO_FULLSCREEN_CHECKBOX),
                    GetModuleHandleW(nullptr),
                    nullptr
                );
                SendMessageW(
                    state->fullscreenCheckbox,
                    BM_SETCHECK,
                    state->owner->m_disableHotkeysInFullscreen ? BST_CHECKED : BST_UNCHECKED,
                    0
                );
                auto* checkboxState = new CheckboxVisualState();
                if (!SetWindowSubclass(
                        state->fullscreenCheckbox,
                        CheckboxPaintSubclassProc,
                        1,
                        reinterpret_cast<DWORD_PTR>(checkboxState))) {
                    delete checkboxState;
                }

                state->blacklistList = CreateWindowExW(
                    0, WC_LISTVIEWW, L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    0, 0, 100, 100,
                    hWnd,
                    reinterpret_cast<HMENU>(ID_INFO_BLACKLIST_LIST),
                    GetModuleHandleW(nullptr),
                    nullptr
                );
                ListView_SetExtendedListViewStyle(
                    state->blacklistList,
                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER
                );
                ListView_SetBkColor(state->blacklistList, RGB(37, 37, 37));
                ListView_SetTextBkColor(state->blacklistList, RGB(37, 37, 37));
                ListView_SetTextColor(state->blacklistList, RGB(245, 245, 245));
                ApplyDarkScrollBar(state->blacklistList);

                LVCOLUMNW column = {};
                column.mask = LVCF_TEXT | LVCF_WIDTH;
                column.cx = 190;
                column.pszText = const_cast<wchar_t*>(T(L"application_blacklist.column.application"));
                ListView_InsertColumn(state->blacklistList, 0, &column);
                column.cx = 500;
                column.pszText = const_cast<wchar_t*>(T(L"application_blacklist.column.path"));
                ListView_InsertColumn(state->blacklistList, 1, &column);
                ApplyDarkListViewHeader(state->blacklistList);

                state->runningPickerButton = CreateWindowExW(
                    0, L"BUTTON", T(L"application_blacklist.running"),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0, 0, 170, 34,
                    hWnd, reinterpret_cast<HMENU>(ID_INFO_RUNNING_PICKER), GetModuleHandleW(nullptr), nullptr
                );
                state->exePickerButton = CreateWindowExW(
                    0, L"BUTTON", T(L"application_blacklist.add_exe"),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0, 0, 130, 34,
                    hWnd, reinterpret_cast<HMENU>(ID_INFO_EXE_PICKER), GetModuleHandleW(nullptr), nullptr
                );
                state->removeButton = CreateWindowExW(
                    0, L"BUTTON", T(L"application_blacklist.remove"),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0, 0, 110, 34,
                    hWnd, reinterpret_cast<HMENU>(ID_INFO_REMOVE_BLACKLIST), GetModuleHandleW(nullptr), nullptr
                );
                EnableWindow(state->removeButton, FALSE);
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
                0, L"BUTTON", T(L"info.button.close"),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 100, 34,
                hWnd, reinterpret_cast<HMENU>(ID_INFO_CLOSE), GetModuleHandleW(nullptr), nullptr
            );

            if (state->kind == static_cast<int>(Application::InfoWindowKind::About)) {
                state->actionButton = CreateWindowExW(
                    0, L"BUTTON", T(L"info.button.check_updates"),
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 180, 34,
                    hWnd, reinterpret_cast<HMENU>(ID_INFO_ACTION), GetModuleHandleW(nullptr), nullptr
                );
            }

            const bool isLogs = state->kind == static_cast<int>(Application::InfoWindowKind::Logs);
            HFONT textFont = isLogs ? state->owner->m_hMonoFont : state->owner->m_hFont;
            SendMessageW(state->titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            if (state->textControl) {
                SendMessageW(state->textControl, WM_SETFONT, reinterpret_cast<WPARAM>(textFont), TRUE);
            }
            if (isLogs && state->richEdit && state->textControl) {
                SendMessageW(
                    state->textControl,
                    EM_SETBKGNDCOLOR,
                    0,
                    static_cast<LPARAM>(RGB(45, 45, 45))
                );
                CHARFORMAT2W format = {};
                format.cbSize = sizeof(format);
                format.dwMask = CFM_COLOR;
                format.crTextColor = RGB(245, 245, 245);
                SendMessageW(state->textControl, EM_SETCHARFORMAT, SCF_DEFAULT,
                    reinterpret_cast<LPARAM>(&format));
                SendMessageW(state->textControl, EM_SETCHARFORMAT, SCF_ALL,
                    reinterpret_cast<LPARAM>(&format));
                ApplyDarkScrollBar(state->textControl, false);
            }
            SendMessageW(state->closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            if (state->actionButton) {
                SendMessageW(state->actionButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            }
            if (state->fullscreenCheckbox) {
                SendMessageW(
                    state->fullscreenCheckbox,
                    WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->owner->m_hFont),
                    TRUE
                );
            }
            if (state->blacklistList) {
                SendMessageW(state->blacklistList, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
                SendMessageW(state->runningPickerButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
                SendMessageW(state->exePickerButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
                SendMessageW(state->removeButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
                state->owner->m_hApplicationBlacklistWindow = hWnd;
                state->owner->RefreshApplicationBlacklistList();
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
            MoveWindow(state->titleLabel, m, m, w - 2 * m, titleH, TRUE);
            if (state->kind == static_cast<int>(Application::InfoWindowKind::ApplicationBlacklist)) {
                MoveWindow(state->fullscreenCheckbox, m, textTop, w - 2 * m, 28, TRUE);
                const int listTop = textTop + 36;
                const int listHeight = std::max(100, h - listTop - m - bh - footerGap);
                const int y = listTop + listHeight + footerGap;
                const int runningW = 180;
                const int exeW = 130;
                const int removeW = 110;
                MoveWindow(state->blacklistList, m, listTop, w - 2 * m, listHeight, TRUE);
                MoveWindow(state->runningPickerButton, m, y, runningW, bh, TRUE);
                MoveWindow(state->exePickerButton, m + runningW + gap, y, exeW, bh, TRUE);
                MoveWindow(state->removeButton, m + runningW + gap + exeW + gap, y, removeW, bh, TRUE);
                MoveWindow(state->closeButton, w - m - closeW, h - m - bh, closeW, bh, TRUE);
                ListView_SetColumnWidth(state->blacklistList, 0, 190);
                ListView_SetColumnWidth(state->blacklistList, 1, std::max(240, w - 2 * m - 194));
                return 0;
            }

            const int textHeight = std::max(70, h - textTop - m - bh - footerGap);
            const int y = textTop + textHeight + footerGap;

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
            if (DrawPaddedListBoxItem(dis)) {
                return TRUE;
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
                const bool isLogs = state->kind == static_cast<int>(Application::InfoWindowKind::Logs);
                ShowStyledContextMenu(hWnd, point, isLogs, isLogs);
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

    case WM_CTLCOLORBTN:
        if (state && reinterpret_cast<HWND>(lParam) == state->fullscreenCheckbox) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
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
        break;

    case WM_NOTIFY:
        if (state) {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->hwndFrom == state->blacklistList) {
                if (header->code == LVN_ITEMCHANGED) {
                    EnableWindow(
                        state->removeButton,
                        ListView_GetNextItem(state->blacklistList, -1, LVNI_SELECTED) >= 0
                    );
                    return 0;
                }
                if (header->code == LVN_KEYDOWN) {
                    const auto* key = reinterpret_cast<NMLVKEYDOWN*>(lParam);
                    if (key->wVKey == VK_DELETE && state->owner) {
                        state->owner->RemoveSelectedApplicationFromBlacklist();
                    }
                    return 0;
                }
                if (header->code == NM_CUSTOMDRAW) {
                    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        const bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
                        draw->clrText = RGB(245, 245, 245);
                        draw->clrTextBk = selected ? RGB(58, 58, 58) : RGB(37, 37, 37);
                        return CDRF_DODEFAULT;
                    }
                }
            }
        }
        break;

    case WM_COMMAND:
        if (state) {
            const UINT id = LOWORD(wParam);
            const UINT notifyCode = HIWORD(wParam);
            if (id == ID_MENU_CONTEXT_COPY) {
                if (state->contextMenuTarget == state->textControl) {
                    CopyEditSelectionOrAll(state->textControl);
                }
                return 0;
            }
            if (id == ID_MENU_CONTEXT_SAVEAS) {
                const bool isLogs = state->kind == static_cast<int>(Application::InfoWindowKind::Logs);
                if (isLogs && state->owner && state->contextMenuTarget == state->textControl) {
                    std::wstring savedPath;
                    std::wstring saveError;
                    if (SaveTextWithDialog(
                            hWnd,
                            LogFile::Read(state->owner->m_logPath),
                            &savedPath,
                            &saveError
                        )) {
                        if (state->owner) {
                            state->owner->AppendLog(std::wstring(T(L"app.log.logs.saved_prefix")) + savedPath);
                        }
                    } else if (!saveError.empty() && state->owner) {
                        state->owner->ShowStyledMessage(T(L"app.title.save_error"), saveError);
                    }
                }
                return 0;
            }
            if (id == ID_MENU_CONTEXT_CLEAR_LOGS) {
                if (state->kind == static_cast<int>(Application::InfoWindowKind::Logs)
                    && state->owner) {
                    state->owner->ClearLogs();
                }
                return 0;
            }
            if (id == ID_INFO_FULLSCREEN_CHECKBOX
                && notifyCode == BN_CLICKED
                && state->owner
                && state->fullscreenCheckbox) {
                state->owner->m_disableHotkeysInFullscreen =
                    SendMessageW(state->fullscreenCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_disableHotkeysInFullscreen = state->owner->m_disableHotkeysInFullscreen;
                SaveDisableFullscreenHotkeysSetting(
                    GetLanguageSettingsPath(state->owner->GetExecutableDirectory()),
                    state->owner->m_disableHotkeysInFullscreen
                );
                return 0;
            }
            if (id == ID_INFO_RUNNING_PICKER && state->owner) {
                state->owner->AddApplicationsToBlacklist(
                    state->owner->SelectRunningApplications()
                );
                return 0;
            }
            if (id == ID_INFO_EXE_PICKER && state->owner) {
                state->owner->AddApplicationsToBlacklist(
                    state->owner->SelectExecutableApplications()
                );
                return 0;
            }
            if (id == ID_INFO_REMOVE_BLACKLIST && state->owner) {
                state->owner->RemoveSelectedApplicationFromBlacklist();
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
    case WM_GETMINMAXINFO:
        if (state && state->runningApplicationSelection) {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = LOGS_MIN_WIDTH;
            info->ptMinTrackSize.y = LOGS_MIN_HEIGHT;
            return 0;
        }
        break;

    case WM_CREATE:
        if (state) {
            state->titleLabel = CreateWindowExW(
                0, L"STATIC", state->title.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 100, 24,
                hWnd, nullptr, GetModuleHandleW(nullptr), nullptr
            );
            state->usesListBox = !state->runningApplicationSelection;
            if (state->runningApplicationSelection) {
                state->textControl = CreateWindowExW(
                    0, WC_LISTVIEWW, nullptr,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                        LVS_REPORT | LVS_SHOWSELALWAYS,
                    0, 0, 100, 100,
                    hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_TEXT), GetModuleHandleW(nullptr), nullptr
                );
                ListView_SetExtendedListViewStyle(
                    state->textControl,
                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP
                );
                ListView_SetBkColor(state->textControl, RGB(37, 37, 37));
                ListView_SetTextBkColor(state->textControl, RGB(37, 37, 37));
                ListView_SetTextColor(state->textControl, RGB(245, 245, 245));

                LVCOLUMNW column = {};
                column.mask = LVCF_TEXT | LVCF_WIDTH;
                column.cx = 170;
                column.pszText = const_cast<wchar_t*>(T(L"application_blacklist.column.application"));
                ListView_InsertColumn(state->textControl, 0, &column);
                column.cx = 280;
                column.pszText = const_cast<wchar_t*>(T(L"application_blacklist.column.window_title"));
                ListView_InsertColumn(state->textControl, 1, &column);
                column.cx = 520;
                column.pszText = const_cast<wchar_t*>(T(L"application_blacklist.column.path"));
                ListView_InsertColumn(state->textControl, 2, &column);

                for (size_t index = 0; index < state->runningApplications.size(); ++index) {
                    const RunningApplication& application = state->runningApplications[index];
                    LVITEMW item = {};
                    item.mask = LVIF_TEXT | LVIF_PARAM;
                    item.iItem = static_cast<int>(index);
                    item.pszText = const_cast<wchar_t*>(application.executableName.c_str());
                    item.lParam = static_cast<LPARAM>(index);
                    const int inserted = ListView_InsertItem(state->textControl, &item);
                    if (inserted >= 0) {
                        ListView_SetItemText(
                            state->textControl,
                            inserted,
                            1,
                            const_cast<wchar_t*>(application.windowTitle.c_str())
                        );
                        ListView_SetItemText(
                            state->textControl,
                            inserted,
                            2,
                            const_cast<wchar_t*>(application.path.c_str())
                        );
                    }
                }
                ApplyDarkScrollBar(state->textControl);
            } else {
                const DWORD listStyle = LBS_NOINTEGRALHEIGHT | LBS_NOSEL;
                state->textControl = CreateWindowExW(
                    0, L"LISTBOX", nullptr,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | listStyle,
                    0, 0, 100, 100,
                    hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_TEXT), GetModuleHandleW(nullptr), nullptr
                );
                if (state->textControl) {
                    ApplyDarkScrollBar(state->textControl);
                    FillListBoxWithWrappedText(state->textControl, state->text);
                    SetWindowSubclass(state->textControl, CopyOnlyContextSubclassProc, 1, reinterpret_cast<DWORD_PTR>(hWnd));
                }
            }
            state->primaryButton = CreateWindowExW(
                0, L"BUTTON", state->primaryButtonText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 120, 34,
                hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_PRIMARY), GetModuleHandleW(nullptr), nullptr
            );
            if (state->hasSecondaryButton) {
                state->secondaryButton = CreateWindowExW(
                    0, L"BUTTON", state->secondaryButtonText.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0, 0, 120, 34,
                    hWnd, reinterpret_cast<HMENU>(ID_MESSAGE_SECONDARY), GetModuleHandleW(nullptr), nullptr
                );
            }
            SendMessageW(state->titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(state->owner->m_hFont), TRUE);
            HFONT textFont = state->useMonoFont ? state->owner->m_hMonoFont : state->owner->m_hFont;
            SendMessageW(state->textControl, WM_SETFONT, reinterpret_cast<WPARAM>(textFont), TRUE);
            if (state->runningApplicationSelection) {
                ApplyDarkListViewHeader(state->textControl);
            }
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
            const int bw = state->runningApplicationSelection ? 180 : 126;
            const int gap = 10;
            const int footerGap = 10;

            const int textTop = m + titleH + 6;
            const int textHeight = std::max(50, h - textTop - m - bh - footerGap);
            const int y = textTop + textHeight + footerGap;

            MoveWindow(state->titleLabel, m, m, w - 2 * m, titleH, TRUE);
            MoveWindow(state->textControl, m, textTop, w - 2 * m, textHeight, TRUE);
            if (state->usesListBox && state->textControl && !state->runningApplicationSelection) {
                FillListBoxWithWrappedText(state->textControl, state->text);
            }
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
            if (sourceControl == state->textControl && !state->runningApplicationSelection) {
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
            if (state && state->textControl) {
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

    case WM_NOTIFY:
        {
            auto* click = reinterpret_cast<NMLISTVIEW*>(lParam);
            if (state && state->runningApplicationSelection && click
                && click->hdr.hwndFrom == state->textControl
                && click->hdr.code == LVN_COLUMNCLICK) {
                if (state->runningSortColumn == click->iSubItem) {
                    state->runningSortAscending = !state->runningSortAscending;
                } else {
                    state->runningSortColumn = click->iSubItem;
                    state->runningSortAscending = true;
                }
                SetRunningApplicationSortIndicator(
                    state->textControl,
                    state->runningSortColumn,
                    state->runningSortAscending
                );
                ListView_SortItemsEx(
                    state->textControl,
                    CompareRunningApplicationRows,
                    reinterpret_cast<LPARAM>(state)
                );
                return 0;
            }
        }
        break;

    case WM_COMMAND:
        if (state) {
            const UINT id = LOWORD(wParam);
            if (id == ID_MENU_CONTEXT_COPY) {
                if (state->contextMenuTarget == state->textControl) {
                    ClipboardUtils::WriteText(hWnd, state->text);
                }
                return 0;
            }
            if (id == ID_MESSAGE_PRIMARY || id == IDOK) {
                if (state->runningApplicationSelection && state->selectedApplicationPathsOut) {
                    for (int row = ListView_GetNextItem(state->textControl, -1, LVNI_SELECTED);
                         row != -1;
                         row = ListView_GetNextItem(state->textControl, row, LVNI_SELECTED)) {
                        LVITEMW item = {};
                        item.mask = LVIF_PARAM;
                        item.iItem = row;
                        if (ListView_GetItem(state->textControl, &item)) {
                            const size_t sourceIndex = static_cast<size_t>(item.lParam);
                            if (sourceIndex < state->runningApplications.size()) {
                                state->selectedApplicationPathsOut->push_back(
                                    state->runningApplications[sourceIndex].path
                                );
                            }
                        }
                    }
                }
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
