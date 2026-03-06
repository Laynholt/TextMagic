#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppVersion.h"
#include "ScriptManifest.h"
#include "ScriptRunner.h"
#include "TextBridge.h"
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
        Logs = 2
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
    void UpdateLanguageMenuChecks();
    void UpdateScriptInputModeMenuChecks();
    void SetScriptInputMode(bool fallbackToAllText);
    void OnResize(int width, int height);
    void OnPaint();
    void OnCommand(UINT controlId, UINT notifyCode);
    void OnMenuCommand(UINT menuId);
    void ShowMoreMenu();
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
    bool ImportScriptFile(const std::wstring& sourcePath, std::wstring* copiedPath) const;

    void ExecuteSelectedScript();
    void ExecuteScriptByHotkeyId(int hotkeyId);
    void ExecuteScript(const RegisteredScript& script);

    bool RegisterInfoWindowClass();
    bool RegisterMessageWindowClass();
    void ShowAboutWindow();
    void ShowLogsWindow();
    void CreateOrActivateInfoWindow(InfoWindowKind kind, HWND& targetHandle, const wchar_t* title, const std::wstring& bodyText);
    void OnInfoWindowClosed(InfoWindowKind kind);
    void UpdateInfoWindowText(InfoWindowKind kind, const std::wstring& text);

    int ShowStyledMessageDialog(const wchar_t* title,
                                const std::wstring& bodyText,
                                const wchar_t* primaryButtonText,
                                const wchar_t* secondaryButtonText = nullptr);
    void ShowStyledMessage(const std::wstring& title, const std::wstring& message);
    void CheckForUpdates();

    void AppendLog(const std::wstring& line);
    void ClearLogs();
    std::wstring BuildAboutText() const;
    std::wstring BuildLogText() const;

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
    HMENU m_hMoreMenu = nullptr;
    HMENU m_hLanguageMenu = nullptr;
    HMENU m_hInputModeMenu = nullptr;

    HFONT m_hTitleFont = nullptr;
    HFONT m_hFont = nullptr;
    HFONT m_hMonoFont = nullptr;

    HBRUSH m_hBackgroundBrush = nullptr;
    HBRUSH m_hCardBrush = nullptr;
    HBRUSH m_hListBrush = nullptr;

    RECT m_cardRect = { 0, 0, 0, 0 };
    RECT m_statusCardRect = { 0, 0, 0, 0 };
    HWND m_hoveredControl = nullptr;
    HWND m_pressedControl = nullptr;

    ULONG_PTR m_gdiplusToken = 0;
    HANDLE m_singleInstanceMutex = nullptr;
    bool m_comInitialized = false;
    bool m_infoWindowClassRegistered = false;
    bool m_messageWindowClassRegistered = false;
    bool m_isExiting = false;
    bool m_scriptExecutionInProgress = false;
    bool m_updateInProgress = false;
    bool m_archiveTaskInProgress = false;
    bool m_scriptInputFallbackToAllText = false;

    NOTIFYICONDATAW m_trayIconData = {};

    std::wstring m_initializationError;
    std::wstring m_scriptsDirectory;
    std::vector<RegisteredScript> m_scripts;
    std::map<int, size_t> m_scriptIndexByHotkeyId;
    std::vector<std::wstring> m_executionLogs;

    std::unique_ptr<ToolTip> m_toolTip;
    std::unique_ptr<UpdateService> m_updateService;
    TextBridge m_textBridge;
    ScriptRunner m_scriptRunner;

    static constexpr int MIN_WINDOW_WIDTH = 760;
    static constexpr int MIN_WINDOW_HEIGHT = 520;
};
