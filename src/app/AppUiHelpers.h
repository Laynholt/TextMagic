#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

bool CopyTextToClipboard(HWND ownerWindow, const std::wstring& text);
void CopyEditSelectionOrAll(HWND editControl);
bool SaveTextWithDialog(HWND ownerWindow, const std::wstring& text, std::wstring* savedPath, std::wstring* error);
void FillListBoxWithText(HWND listBox, const std::wstring& text);
void FillListBoxWithWrappedText(HWND listBox, const std::wstring& text, bool scrollToBottom = false);
POINT ResolveContextMenuPoint(HWND control, LPARAM lParam);
std::wstring GetSelectedListBoxText(HWND listBox);
bool IsTmscriptFilePath(const std::filesystem::path& path);
