#include "UiRenderer.h"

#include <algorithm>
#include <gdiplus.h>

using namespace Gdiplus;

namespace {
constexpr int POPUP_ITEM_HEIGHT = 34;
constexpr int POPUP_SEPARATOR_HEIGHT = 10;
constexpr int POPUP_TEXT_PADDING_LEFT = 14;
constexpr int POPUP_CHECK_PADDING_LEFT = 28;
constexpr int POPUP_BORDER_COLOR = 62;
}

void UiRenderer::DrawCustomButton(HDC hdc, HWND button, const std::wstring& text, bool isPressed, float hoverAlpha) {
    RECT rect;
    GetClientRect(button, &rect);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

    Graphics graphics(memDC);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    graphics.Clear(Color(255, 45, 45, 45));

    Color baseColor = Color(255, 68, 68, 68);
    Color hoverColor = Color(255, 78, 78, 78);
    Color pressedColor = Color(255, 58, 58, 58);

    Color baseBorder = Color(255, 85, 85, 85);
    Color hoverBorder = Color(255, 100, 100, 100);
    Color pressedBorder = Color(255, 80, 80, 80);

    Color baseText = Color(255, 230, 230, 230);
    Color hoverText = Color(255, 255, 255, 255);
    Color pressedText = Color(255, 220, 220, 220);

    Color bgColor;
    Color borderColor;
    Color textColor;
    if (isPressed) {
        bgColor = pressedColor;
        borderColor = pressedBorder;
        textColor = pressedText;
    } else {
        BYTE bgR = static_cast<BYTE>(baseColor.GetRed() + (hoverColor.GetRed() - baseColor.GetRed()) * hoverAlpha);
        BYTE bgG = static_cast<BYTE>(baseColor.GetGreen() + (hoverColor.GetGreen() - baseColor.GetGreen()) * hoverAlpha);
        BYTE bgB = static_cast<BYTE>(baseColor.GetBlue() + (hoverColor.GetBlue() - baseColor.GetBlue()) * hoverAlpha);
        bgColor = Color(255, bgR, bgG, bgB);

        BYTE borderR = static_cast<BYTE>(baseBorder.GetRed() + (hoverBorder.GetRed() - baseBorder.GetRed()) * hoverAlpha);
        BYTE borderG = static_cast<BYTE>(baseBorder.GetGreen() + (hoverBorder.GetGreen() - baseBorder.GetGreen()) * hoverAlpha);
        BYTE borderB = static_cast<BYTE>(baseBorder.GetBlue() + (hoverBorder.GetBlue() - baseBorder.GetBlue()) * hoverAlpha);
        borderColor = Color(255, borderR, borderG, borderB);

        BYTE textR = static_cast<BYTE>(baseText.GetRed() + (hoverText.GetRed() - baseText.GetRed()) * hoverAlpha);
        BYTE textG = static_cast<BYTE>(baseText.GetGreen() + (hoverText.GetGreen() - baseText.GetGreen()) * hoverAlpha);
        BYTE textB = static_cast<BYTE>(baseText.GetBlue() + (hoverText.GetBlue() - baseText.GetBlue()) * hoverAlpha);
        textColor = Color(255, textR, textG, textB);
    }

    SolidBrush bgBrush(bgColor);

    float radius = 4.0f;
    float x = 0.5f;
    float y = 0.5f;
    float width = static_cast<float>(rect.right) - 1.0f;
    float height = static_cast<float>(rect.bottom) - 1.0f;

    GraphicsPath path;
    path.AddArc(x, y, radius * 2.0f, radius * 2.0f, 180.0f, 90.0f);
    path.AddLine(x + radius, y, x + width - radius, y);
    path.AddArc(x + width - radius * 2.0f, y, radius * 2.0f, radius * 2.0f, 270.0f, 90.0f);
    path.AddLine(x + width, y + radius, x + width, y + height - radius);
    path.AddArc(x + width - radius * 2.0f, y + height - radius * 2.0f, radius * 2.0f, radius * 2.0f, 0.0f, 90.0f);
    path.AddLine(x + width - radius, y + height, x + radius, y + height);
    path.AddArc(x, y + height - radius * 2.0f, radius * 2.0f, radius * 2.0f, 90.0f, 90.0f);
    path.AddLine(x, y + height - radius, x, y + radius);
    path.CloseFigure();

    graphics.FillPath(&bgBrush, &path);

    Pen borderPen(borderColor, 1.0f);
    graphics.DrawPath(&borderPen, &path);

    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 12, FontStyleRegular, UnitPoint);
    SolidBrush textBrush(textColor);

    RectF textRect(
        static_cast<REAL>(rect.left),
        static_cast<REAL>(rect.top),
        static_cast<REAL>(rect.right - rect.left),
        static_cast<REAL>(rect.bottom - rect.top)
    );
    StringFormat stringFormat;
    stringFormat.SetAlignment(StringAlignmentCenter);
    stringFormat.SetLineAlignment(StringAlignmentCenter);

    graphics.DrawString(text.c_str(), -1, &font, textRect, &stringFormat, &textBrush);

    BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

void UiRenderer::DrawCustomCheckbox(HDC hdc, HWND control, const std::wstring& text, bool checked, bool hot, bool pressed, bool enabled, bool focused) {
    RECT rect;
    GetClientRect(control, &rect);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

    Graphics graphics(memDC);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.Clear(Color(255, 45, 45, 45));

    const Color textColor = enabled ? Color(255, 235, 235, 235) : Color(255, 145, 145, 145);
    const Color boxBg = pressed ? Color(255, 35, 35, 35) : (hot ? Color(255, 50, 50, 50) : Color(255, 40, 40, 40));
    const Color boxBorder = checked ? Color(255, 135, 170, 220) : Color(255, 92, 92, 92);

    const REAL boxSize = 14.0f;
    const REAL boxX = 6.0f;
    const REAL boxY = (static_cast<REAL>(rect.bottom - rect.top) - boxSize) * 0.5f;

    GraphicsPath boxPath;
    const REAL radius = 3.0f;
    boxPath.AddArc(boxX, boxY, radius * 2.0f, radius * 2.0f, 180.0f, 90.0f);
    boxPath.AddArc(boxX + boxSize - radius * 2.0f, boxY, radius * 2.0f, radius * 2.0f, 270.0f, 90.0f);
    boxPath.AddArc(boxX + boxSize - radius * 2.0f, boxY + boxSize - radius * 2.0f, radius * 2.0f, radius * 2.0f, 0.0f, 90.0f);
    boxPath.AddArc(boxX, boxY + boxSize - radius * 2.0f, radius * 2.0f, radius * 2.0f, 90.0f, 90.0f);
    boxPath.CloseFigure();

    SolidBrush boxBrush(boxBg);
    graphics.FillPath(&boxBrush, &boxPath);
    Pen borderPen(boxBorder, 1.0f);
    graphics.DrawPath(&borderPen, &boxPath);

    if (checked) {
        Pen checkPen(Color(255, 220, 235, 255), 2.0f);
        checkPen.SetStartCap(LineCapRound);
        checkPen.SetEndCap(LineCapRound);
        graphics.DrawLine(&checkPen, boxX + 3.0f, boxY + 7.5f, boxX + 6.0f, boxY + 10.5f);
        graphics.DrawLine(&checkPen, boxX + 6.0f, boxY + 10.5f, boxX + 11.0f, boxY + 4.0f);
    }

    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 11, FontStyleRegular, UnitPoint);
    SolidBrush textBrush(textColor);

    RectF textRect(
        boxX + boxSize + 6.0f,
        0.0f,
        static_cast<REAL>(rect.right - rect.left) - (boxX + boxSize + 6.0f),
        static_cast<REAL>(rect.bottom - rect.top)
    );
    StringFormat stringFormat;
    stringFormat.SetAlignment(StringAlignmentNear);
    stringFormat.SetLineAlignment(StringAlignmentCenter);
    graphics.DrawString(text.c_str(), -1, &font, textRect, &stringFormat, &textBrush);

    if (focused) {
        Pen focusPen(Color(180, 125, 125, 125), 1.0f);
        graphics.DrawRectangle(&focusPen, 1.0f, 1.0f, static_cast<REAL>(rect.right - rect.left - 3), static_cast<REAL>(rect.bottom - rect.top - 3));
    }

    BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

void UiRenderer::DrawBackground(HDC hdc, const RECT& rect) {
    HBRUSH bgBrush = CreateSolidBrush(RGB(26, 26, 26));
    FillRect(hdc, &rect, bgBrush);
    DeleteObject(bgBrush);
}

void UiRenderer::DrawCard(HDC hdc, const RECT& rect, const std::wstring& title) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    Color cardBg(255, 45, 45, 45);
    Color borderColor(255, 64, 64, 64);

    GraphicsPath path;
    int radius = 8;
    path.AddArc(rect.left, rect.top, radius * 2, radius * 2, 180, 90);
    path.AddArc(rect.right - radius * 2, rect.top, radius * 2, radius * 2, 270, 90);
    path.AddArc(rect.right - radius * 2, rect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(rect.left, rect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    GraphicsPath shadowPath;
    int shadowOffset = 2;
    shadowPath.AddArc(rect.left + shadowOffset, rect.top + shadowOffset, radius * 2, radius * 2, 180, 90);
    shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, rect.top + shadowOffset, radius * 2, radius * 2, 270, 90);
    shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 0, 90);
    shadowPath.AddArc(rect.left + shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 90, 90);
    shadowPath.CloseFigure();

    SolidBrush shadowBrush(Color(76, 0, 0, 0));
    graphics.FillPath(&shadowBrush, &shadowPath);

    SolidBrush cardBrush(cardBg);
    graphics.FillPath(&cardBrush, &path);

    Pen borderPen(borderColor, 1.0f);
    graphics.DrawPath(&borderPen, &path);

    if (!title.empty()) {
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 12, FontStyleBold, UnitPoint);
        SolidBrush textBrush(Color(255, 255, 255, 255));

        RectF titleRect(
            static_cast<REAL>(rect.left) + 16,
            static_cast<REAL>(rect.top) + 6,
            static_cast<REAL>(rect.right - rect.left - 32),
            24
        );
        StringFormat stringFormat;
        stringFormat.SetAlignment(StringAlignmentNear);
        stringFormat.SetLineAlignment(StringAlignmentCenter);

        graphics.DrawString(title.c_str(), -1, &font, titleRect, &stringFormat, &textBrush);
    }
}

void UiRenderer::DrawRoundedPanel(HDC hdc, const RECT& rect, COLORREF background, COLORREF border, int radius) {
    if (!hdc || rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int safeRadius = (std::max)(0, (std::min)(radius, (std::min)(width, height) / 2));
    const REAL diameter = static_cast<REAL>(safeRadius * 2);
    const REAL left = static_cast<REAL>(rect.left) + 0.5f;
    const REAL top = static_cast<REAL>(rect.top) + 0.5f;
    const REAL right = static_cast<REAL>(rect.right) - 0.5f;
    const REAL bottom = static_cast<REAL>(rect.bottom) - 0.5f;

    GraphicsPath path;
    if (safeRadius == 0) {
        path.AddRectangle(RectF(left, top, right - left, bottom - top));
    } else {
        path.AddArc(left, top, diameter, diameter, 180.0f, 90.0f);
        path.AddArc(right - diameter, top, diameter, diameter, 270.0f, 90.0f);
        path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0.0f, 90.0f);
        path.AddArc(left, bottom - diameter, diameter, diameter, 90.0f, 90.0f);
        path.CloseFigure();
    }

    SolidBrush panelBrush(Color(255, GetRValue(background), GetGValue(background), GetBValue(background)));
    Pen borderPen(Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.0f);
    graphics.FillPath(&panelBrush, &path);
    graphics.DrawPath(&borderPen, &path);
}

void UiRenderer::DrawEditBorder(HWND parentWindow, HWND editControl, int padding) {
    if (!editControl || !parentWindow) {
        return;
    }

    RECT rect;
    GetWindowRect(editControl, &rect);
    ScreenToClient(parentWindow, reinterpret_cast<LPPOINT>(&rect.left));
    ScreenToClient(parentWindow, reinterpret_cast<LPPOINT>(&rect.right));
    InflateRect(&rect, (std::max)(0, padding), (std::max)(0, padding));

    HDC hdc = GetDC(parentWindow);
    if (!hdc) {
        return;
    }

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(62, 62, 62));
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));

    MoveToEx(hdc, rect.left - 1, rect.top - 1, nullptr);
    LineTo(hdc, rect.right, rect.top - 1);
    LineTo(hdc, rect.right, rect.bottom);
    LineTo(hdc, rect.left - 1, rect.bottom);
    LineTo(hdc, rect.left - 1, rect.top - 1);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    ReleaseDC(parentWindow, hdc);
}

void UiRenderer::DrawMenuCheckMark(HDC hdc, const RECT& itemRect, COLORREF color) {
    if (!hdc) {
        return;
    }

    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);

    Pen pen(
        Color(255, GetRValue(color), GetGValue(color), GetBValue(color)),
        2.2f
    );
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    const int left = itemRect.left + 10;
    const int cy = (itemRect.top + itemRect.bottom) / 2;
    const PointF points[] = {
        PointF(static_cast<REAL>(left), static_cast<REAL>(cy)),
        PointF(static_cast<REAL>(left + 4), static_cast<REAL>(cy + 4)),
        PointF(static_cast<REAL>(left + 11), static_cast<REAL>(cy - 5))
    };
    graphics.DrawLines(&pen, points, static_cast<INT>(_countof(points)));
}

void UiRenderer::DrawMenuChevron(HDC hdc, const RECT& itemRect, COLORREF color) {
    if (!hdc) {
        return;
    }

    HPEN arrowPen = CreatePen(PS_SOLID, 2, color);
    HPEN oldArrowPen = static_cast<HPEN>(SelectObject(hdc, arrowPen));
    const int cx = itemRect.right - 18;
    const int cy = (itemRect.top + itemRect.bottom) / 2;
    MoveToEx(hdc, cx - 3, cy - 4, nullptr);
    LineTo(hdc, cx + 1, cy);
    LineTo(hdc, cx - 3, cy + 4);
    SelectObject(hdc, oldArrowPen);
    DeleteObject(arrowPen);
}

void UiRenderer::DrawPopupMenu(HDC hdc, const RECT& rect, const std::vector<PopupMenuItem>& items, UINT hoveredItemId) {
    if (!hdc) {
        return;
    }

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(45, 45, 45));
    FillRect(hdc, &rect, backgroundBrush);
    DeleteObject(backgroundBrush);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(POPUP_BORDER_COLOR, POPUP_BORDER_COLOR, POPUP_BORDER_COLOR));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
    MoveToEx(hdc, rect.left, rect.top, nullptr);
    LineTo(hdc, rect.right - 1, rect.top);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.top);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    int top = 1;
    for (const PopupMenuItem& item : items) {
        const int itemHeight = item.separator ? POPUP_SEPARATOR_HEIGHT : POPUP_ITEM_HEIGHT;
        RECT itemRect = { 1, top, rect.right - 1, top + itemHeight };
        top += itemHeight;

        if (item.separator) {
            HPEN separatorPen = CreatePen(PS_SOLID, 1, RGB(78, 78, 78));
            HPEN oldSeparatorPen = static_cast<HPEN>(SelectObject(hdc, separatorPen));
            const int y = (itemRect.top + itemRect.bottom) / 2;
            MoveToEx(hdc, itemRect.left + 11, y, nullptr);
            LineTo(hdc, itemRect.right - 11, y);
            SelectObject(hdc, oldSeparatorPen);
            DeleteObject(separatorPen);
            continue;
        }

        const bool selected = item.id == hoveredItemId;
        if (selected) {
            HBRUSH selectedBrush = CreateSolidBrush(RGB(66, 66, 66));
            FillRect(hdc, &itemRect, selectedBrush);
            DeleteObject(selectedBrush);
        }

        RECT textRect = itemRect;
        textRect.left += item.checked ? POPUP_CHECK_PADDING_LEFT : POPUP_TEXT_PADDING_LEFT;
        textRect.right -= item.submenu ? 28 : 10;

        const COLORREF textColor = RGB(235, 235, 235);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);
        if (item.checked) {
            DrawMenuCheckMark(hdc, itemRect, textColor);
        }

        DrawTextW(hdc, item.text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (item.submenu) {
            DrawMenuChevron(hdc, itemRect, selected ? RGB(175, 175, 175) : RGB(128, 128, 128));
        }
    }
}
