#pragma once

#include <windows.h>
#include <string>
#include <vector>

class UiRenderer {
public:
    struct PopupMenuItem {
        UINT id = 0;
        std::wstring text;
        bool separator = false;
        bool checked = false;
        bool submenu = false;
    };

    static void DrawCustomButton(HDC hdc, HWND button, const std::wstring& text, bool isPressed, float hoverAlpha);
    static void DrawCustomCheckbox(HDC hdc, HWND control, const std::wstring& text, bool checked, bool hot, bool pressed, bool enabled, bool focused);
    static void DrawBackground(HDC hdc, const RECT& rect);
    static void DrawCard(HDC hdc, const RECT& rect, const std::wstring& title = L"");
    static void DrawRoundedPanel(HDC hdc, const RECT& rect, COLORREF background, COLORREF border, int radius = 10);
    static void DrawRoundedControlFrame(
        HWND parentWindow,
        HWND control,
        int padding,
        COLORREF background,
        COLORREF border,
        int radius = 10);
    static void DrawMenuCheckMark(HDC hdc, const RECT& itemRect, COLORREF color);
    static void DrawMenuChevron(HDC hdc, const RECT& itemRect, COLORREF color);
    static void DrawPopupMenu(HDC hdc, const RECT& rect, const std::vector<PopupMenuItem>& items, UINT hoveredItemId);
};
