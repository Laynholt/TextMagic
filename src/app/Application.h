#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "ApplicationBlacklist.h"
#include "AppVersion.h"
#include "CompletionDelivery.h"
#include "ScriptExecutionGate.h"
#include "ScriptManifest.h"
#include "ScriptRunner.h"
#include "TextBridge.h"
#include "UiRenderer.h"
#include "UpdateService.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class ToolTip;

class Application {
public:
    static constexpr const wchar_t* WINDOW_TITLE = TM_APP_NAME_W;
    static constexpr const wchar_t* APP_VERSION = TM_APP_VERSION_W;
    static constexpr const wchar_t* INIT_ERROR_ALREADY_RUNNING = L"__already_running__";

    Application();
    ~Application();

    bool Initialize(HINSTANCE hInstance);
    int Run();
    void Shutdown();

    HWND GetMainWindow() const { return m_hWnd; }
    const std::wstring& GetInitializationError() const { return m_initializationError; }

private:
    enum class InfoWindowKind {
        About = 1,
        Logs = 2,
        ApplicationBlacklist = 3
    };

    struct RegisteredScript {
        ScriptManifest::Entry manifest;
        int hotkeyId = 0;
        bool hotkeyRegistered = false;
        std::wstring hotkeyError;
    };

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK InfoWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MessageWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void CreateMoreMenu();
    void ApplyLocalization();
    void SetLanguage(const std::wstring& languageCode);
    void UpdateScriptInputModeMenuChecks();
    void SetScriptInputMode(bool allTextInputMode);
    void OnResize(int width, int height);
    void OnPaint();
    void OnCommand(UINT controlId, UINT notifyCode);
    void OnMenuCommand(UINT menuId);
    void ShowMoreMenu();
    bool RegisterMorePopupWindowClass();
    static LRESULT CALLBACK MorePopupWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMorePopupMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool HandleMorePopupKey(UINT virtualKey);
    void CloseMorePopupWindows();
    void CloseMoreSubPopupWindow();
    void EnsureMoreSubPopup(UINT headerItemId);
    void StartMoreMenuTracking();
    void StopMoreMenuTracking();
    void UpdateMorePopupTracking();
    bool HandleMorePopupMouseDown(POINT screenPoint);
    static LRESULT CALLBACK MorePopupMouseHookProc(int code, WPARAM wParam, LPARAM lParam);
    std::vector<UiRenderer::PopupMenuItem> BuildMainMorePopupItems() const;
    std::vector<UiRenderer::PopupMenuItem> BuildSubMorePopupItems(UINT headerItemId) const;
    SIZE MeasureMorePopupWindow(const std::vector<UiRenderer::PopupMenuItem>& items) const;
    int HitTestMorePopupItem(HWND popupWindow, const std::vector<UiRenderer::PopupMenuItem>& items, POINT screenPoint) const;
    RECT GetMorePopupItemRect(HWND popupWindow, const std::vector<UiRenderer::PopupMenuItem>& items, size_t index) const;
    bool IsPointInWindow(HWND windowHandle, POINT screenPoint) const;
    bool IsPointInMoreButton(POINT screenPoint) const;
    bool InitializeTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayContextMenu(POINT screenPoint);
    void HideToTray();
    void RestoreFromTray();
    void ExitApplication();

    void ReloadScripts(bool announceResult);
    void RegisterHotkeys();
    void UnregisterHotkeys();
    void RefreshScriptList();
    std::vector<size_t> GetSelectedScriptIndices() const;
    bool GetPrimarySelectedScriptIndex(size_t* selectedIndex) const;
    void ShowScriptListContextMenu(POINT screenPoint);
    void SetSelectedScriptsEnabled(bool enabled);
    void AddScriptViaDialog();
    void RemoveSelectedScripts();
    void ImportScriptsFromZip();
    void ExportScriptsToZip();
    void ImportScriptFiles(const std::vector<std::wstring>& filePaths);

    void ExecuteSelectedScript();
    void ExecuteScriptByHotkeyId(int hotkeyId, HWND contextWindow = nullptr);
    void ExecuteScript(const RegisteredScript& script,
                       bool clipboardOnly,
                       bool reservationHeld = false,
                       HWND contextWindow = nullptr);
    void ExecuteBuiltinAction(ScriptManifest::Action action, HWND contextWindow);
    bool CycleForegroundKeyboardLayout(HWND contextWindow);

    bool RegisterInfoWindowClass();
    bool RegisterMessageWindowClass();
    void ShowAboutWindow();
    void ShowLogsWindow();
    void ShowApplicationBlacklistWindow();
    void CreateOrActivateInfoWindow(InfoWindowKind kind, HWND& targetHandle, const wchar_t* title, const std::wstring& bodyText);
    void OnInfoWindowClosed(InfoWindowKind kind);
    void UpdateInfoWindowText(InfoWindowKind kind, const std::wstring& text);
    void RefreshApplicationBlacklistList();
    void AddApplicationsToBlacklist(const std::vector<std::wstring>& paths);
    void RemoveSelectedApplicationFromBlacklist();
    bool PublishApplicationBlacklist(ApplicationBlacklist updated);
    std::vector<std::wstring> SelectRunningApplications();
    std::vector<std::wstring> SelectExecutableApplications();

    int ShowStyledMessageDialog(const wchar_t* title,
                                const std::wstring& bodyText,
                                const wchar_t* primaryButtonText,
                                const wchar_t* secondaryButtonText = nullptr,
                                bool secondaryCopiesText = false);
    void ShowStyledMessage(const std::wstring& title, const std::wstring& message);
    void CheckForUpdates();

    void AppendLog(const std::wstring& line);
    void ClearLogs();
    void RefreshLogsWindow();
    void RefreshAboutWindow();
    void CopySelectedLogRows();
    void CopyAllLogRows();
    void SelectAllLogRows();
    void UpdateLogScrollbar();
    void SetStatusText(const std::wstring& text);
    void OpenScriptsFolder() const;
    std::wstring GetExecutableDirectory() const;
    std::wstring GetExecutablePath() const;

    void UpdateHoverState(POINT clientPoint);
    bool IsPointInControl(HWND control, POINT clientPoint) const;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;

    HWND m_hTitleLabel = nullptr;
    HWND m_hHintLabel = nullptr;
    HWND m_hScriptList = nullptr;
    HWND m_hReloadButton = nullptr;
    HWND m_hOpenFolderButton = nullptr;
    HWND m_hMoreButton = nullptr;
    HWND m_hStatusLabel = nullptr;

    HWND m_hAboutWindow = nullptr;
    HWND m_hLogsWindow = nullptr;
    HWND m_hApplicationBlacklistWindow = nullptr;
    HWND m_hMorePopupWindow = nullptr;
    HWND m_hMoreSubPopupWindow = nullptr;

    HFONT m_hTitleFont = nullptr;
    HFONT m_hHintFont = nullptr;
    HFONT m_hFont = nullptr;
    HFONT m_hMonoFont = nullptr;

    HBRUSH m_hBackgroundBrush = nullptr;
    HBRUSH m_hCardBrush = nullptr;
    HBRUSH m_hListBrush = nullptr;
    HMODULE m_msfteditModule = nullptr;

    RECT m_cardRect = { 0, 0, 0, 0 };
    RECT m_statusCardRect = { 0, 0, 0, 0 };
    HWND m_hoveredControl = nullptr;
    HWND m_pressedControl = nullptr;

    ULONG_PTR m_gdiplusToken = 0;
    HANDLE m_singleInstanceMutex = nullptr;
    bool m_comInitialized = false;
    bool m_infoWindowClassRegistered = false;
    bool m_messageWindowClassRegistered = false;
    bool m_morePopupWindowClassRegistered = false;
    bool m_isExiting = false;
    bool m_updateInProgress = false;
    bool m_archiveTaskInProgress = false;
    bool m_scriptInputAllText = false;
    bool m_disableHotkeysInFullscreen = false;
    UINT m_activeMoreSubMenuHeaderId = 0;
    UINT m_hoveredMorePopupItemId = 0;
    UINT m_hoveredMoreSubPopupItemId = 0;
    HHOOK m_morePopupMouseHook = nullptr;

    NOTIFYICONDATAW m_trayIconData = {};

    std::wstring m_initializationError;
    std::wstring m_scriptsDirectory;
    std::wstring m_logPath;
    std::wstring m_blacklistPath;
    std::vector<RegisteredScript> m_scripts;
    std::map<int, size_t> m_scriptIndexByHotkeyId;
    std::vector<UiRenderer::PopupMenuItem> m_morePopupItems;
    std::vector<UiRenderer::PopupMenuItem> m_moreSubPopupItems;

    std::unique_ptr<ToolTip> m_toolTip;
    std::shared_ptr<CompletionRegistry> m_completionRegistry =
        std::make_shared<CompletionRegistry>();
    UpdateService m_updateService;
    TextBridge m_textBridge;
    ScriptRunner m_scriptRunner;
    ApplicationBlacklist m_applicationBlacklist;
    ScriptExecutionGate m_scriptExecutionGate;

    static Application* s_morePopupMouseHookOwner;

    static constexpr int MIN_WINDOW_WIDTH = 760;
    static constexpr int MIN_WINDOW_HEIGHT = 520;
};
