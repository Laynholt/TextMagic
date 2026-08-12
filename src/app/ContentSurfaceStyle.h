#pragma once

#include <windows.h>
#include <commctrl.h>

namespace content_surface_style {
constexpr int kCornerRadius = 10;
constexpr int kDefaultRegionInset = 1;
constexpr int kLogsRegionInset = 2;
constexpr int kTableRegionInset = 2;
constexpr COLORREF kListFill = RGB(37, 37, 37);
constexpr COLORREF kListBorder = RGB(62, 62, 62);
constexpr COLORREF kListText = RGB(245, 245, 245);
constexpr COLORREF kListSelectedFill = RGB(35, 105, 68);
constexpr COLORREF kListSelectedText = RGB(255, 255, 255);
constexpr COLORREF kMessageFill = RGB(42, 42, 44);
constexpr COLORREF kMessageBorder = RGB(55, 55, 58);

struct ListRowVisual {
    COLORREF fill;
    COLORREF text;
};

struct ListRowPaint {
    ListRowVisual visual;
    UINT itemState;
};

constexpr DWORD StripListViewFrameStyle(DWORD style) {
    return style & ~static_cast<DWORD>(WS_BORDER);
}

constexpr DWORD StripListViewFrameExStyle(DWORD exStyle) {
    return exStyle & ~static_cast<DWORD>(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
}

constexpr ListRowVisual ResolveListRowVisual(bool selected) {
    return selected
        ? ListRowVisual{kListSelectedFill, kListSelectedText}
        : ListRowVisual{kListFill, kListText};
}

constexpr ListRowPaint ResolveListRowPaint(UINT itemState, bool selected) {
    return {
        ResolveListRowVisual(selected),
        itemState & ~static_cast<UINT>(CDIS_SELECTED | CDIS_HOT),
    };
}

enum class ScrollbarSurface {
    BlacklistTable,
    RunningPickerTable,
    LogsList,
    GenericMessageList,
};

constexpr bool UsesExplorerScrollbarTheme(ScrollbarSurface) {
    return true;
}
}
