#include "InfoWindowLayout.h"
#include "../src/app/ContentSurfaceStyle.h"
#include "../src/app/MainWindowLayout.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool IsInside(const InfoRect& child, int width, int height) {
    return child.x >= 0 && child.y >= 0
        && child.x + child.width <= width
        && child.y + child.height <= height;
}
}

int main() {
    const LogsWindowLayout logs = CalculateLogsWindowLayout(900, 600);
    Check(logs.title.height == 32,
          "logs title reserves the main-heading height");
    Check(logs.subtitle.height == 18,
          "logs subtitle uses the compact supporting height");
    Check(logs.subtitle.y > logs.title.y + logs.title.height,
          "logs subtitle follows the enlarged title");
    Check(logs.subtitle.y > logs.title.y + logs.title.height,
          "logs subtitle follows title");
    Check(logs.content.y > logs.subtitle.y + logs.subtitle.height,
          "logs panel follows subtitle");
    Check(logs.copyAllButton.y > logs.content.y + logs.content.height,
          "logs footer follows panel");
    Check(IsInside(logs.closeButton, 900, 600), "logs close button stays inside");

    Check(info_window_layout_detail::kLogsMinimumSubtitleWidth == 392,
          "logs subtitle minimum width is explicit");
    Check(info_window_layout_detail::kLogsMinimumClientWidth == 424,
          "logs client minimum width includes outer insets");
    const LogsWindowLayout minimumWidthLogs = CalculateLogsWindowLayout(
        info_window_layout_detail::kLogsMinimumClientWidth, 600);
    Check(minimumWidthLogs.subtitle.width
              >= info_window_layout_detail::kLogsMinimumSubtitleWidth,
          "logs subtitle keeps supporting text width at the minimum outer width");

    const int logsMinimumClientHeight = info_window_layout_detail::kLogsMinimumClientHeight;
    const LogsWindowLayout minimumLogs = CalculateLogsWindowLayout(400, logsMinimumClientHeight);
    Check(minimumLogs.content.height == info_window_layout_detail::kLogsContentMinimumHeight,
          "logs minimum client height keeps the minimum content size");
    Check(minimumLogs.content.y + minimumLogs.content.height
              + info_window_layout_detail::kFooterGap
              <= minimumLogs.copyAllButton.y,
          "logs minimum client height keeps content above the footer");

    const LogsWindowLayout tooShortLogs = CalculateLogsWindowLayout(
        400, logsMinimumClientHeight - 1);
    Check(tooShortLogs.content.height >= info_window_layout_detail::kLogsContentMinimumHeight,
          "logs preserve the minimum content height");
    Check(tooShortLogs.content.y + tooShortLogs.content.height
              + info_window_layout_detail::kFooterGap
              <= tooShortLogs.copyAllButton.y,
          "logs minimum content stays above the footer");

    const AboutWindowLayout about = CalculateAboutWindowLayout(620, 440);
    Check(about.versionLine.width == 132,
          "about version chip stays compact");
    Check(about.versionLine.height == 26,
          "about version chip reserves a complete text line");
    Check(about.detailsPanel.height == 174,
          "about softened card reserves expanded row heights");
    Check(about.loadedScriptsLabel.height == 24,
          "about loaded label leaves descender space");
    Check(about.loadedScriptsValue.height == 26,
          "about loaded value leaves descender space");
    Check(about.directoryLabel.height == 24,
          "about directory label leaves descender space");
    Check(info_window_layout_detail::kAboutMinimumClientHeight == 428,
          "about minimum client height derives from expanded sections");
    Check(about.versionLine.y > about.description.y + about.description.height,
          "about version follows description");
    Check(about.detailsPanel.y > about.versionLine.y + about.versionLine.height,
          "about information card follows version");
    Check(about.loadedScriptsLabel.x == about.loadedScriptsValue.x,
          "about loaded scripts remain left aligned");
    Check(about.loadedScriptsValue.y > about.loadedScriptsLabel.y,
          "about loaded value follows its label");
    Check(about.divider.y > about.loadedScriptsValue.y + about.loadedScriptsValue.height,
          "about divider follows loaded scripts");
    Check(about.directoryLabel.x == about.directoryValue.x,
          "about directory remains left aligned");
    Check(about.directoryLabel.y > about.divider.y,
          "about directory follows divider");
    Check(about.directoryValue.y > about.directoryLabel.y,
          "about directory value follows its label");
    Check(about.actionButton.x == info_window_layout_detail::kOuterInset,
          "about update action anchors left");
    Check(about.actionButton.y == about.closeButton.y,
          "about buttons share a baseline");
    Check(IsInside(about.closeButton, 620, 440), "about close button stays inside");

    const int aboutMinimumClientHeight =
        info_window_layout_detail::kAboutMinimumClientHeight;
    const AboutWindowLayout minimumAbout =
        CalculateAboutWindowLayout(620, aboutMinimumClientHeight);
    Check(IsInside(minimumAbout.title, 620, aboutMinimumClientHeight),
          "about minimum title stays inside");
    Check(IsInside(minimumAbout.description, 620, aboutMinimumClientHeight),
          "about minimum description stays inside");
    Check(IsInside(minimumAbout.versionLine, 620, aboutMinimumClientHeight),
          "about minimum version stays inside");
    Check(IsInside(minimumAbout.detailsPanel, 620, aboutMinimumClientHeight),
          "about minimum information card stays inside");
    Check(IsInside(minimumAbout.loadedScriptsLabel, 620, aboutMinimumClientHeight),
          "about minimum loaded label stays inside");
    Check(IsInside(minimumAbout.loadedScriptsValue, 620, aboutMinimumClientHeight),
          "about minimum loaded value stays inside");
    Check(IsInside(minimumAbout.divider, 620, aboutMinimumClientHeight),
          "about minimum divider stays inside");
    Check(IsInside(minimumAbout.directoryLabel, 620, aboutMinimumClientHeight),
          "about minimum directory label stays inside");
    Check(IsInside(minimumAbout.directoryValue, 620, aboutMinimumClientHeight),
          "about minimum directory value stays inside");
    Check(IsInside(minimumAbout.hint, 620, aboutMinimumClientHeight),
          "about minimum hint stays inside");
    Check(IsInside(minimumAbout.actionButton, 620, aboutMinimumClientHeight),
          "about minimum action button stays inside");
    Check(IsInside(minimumAbout.closeButton, 620, aboutMinimumClientHeight),
          "about minimum close button stays inside");
    Check(minimumAbout.hint.y + minimumAbout.hint.height
              <= minimumAbout.actionButton.y,
          "about minimum hint stays above footer");

    Check(info_window_layout_detail::kBlacklistInitialOuterWidth == 760,
          "blacklist initial width remains approved");
    Check(info_window_layout_detail::kBlacklistInitialOuterHeight == 560,
          "blacklist initial height gains vertical room");
    Check(info_window_layout_detail::kBlacklistMinimumOuterWidth == 640,
          "blacklist minimum width remains approved");
    Check(info_window_layout_detail::kBlacklistMinimumOuterHeight == 460,
          "blacklist minimum height gains vertical room");

    const int blacklistMinimumClientHeight =
        info_window_layout_detail::kBlacklistMinimumClientHeight;
    const BlacklistWindowLayout blacklist = CalculateBlacklistWindowLayout(
        640, blacklistMinimumClientHeight);
    Check(blacklist.title.height == 40,
          "blacklist title leaves room for descenders");
    Check(blacklist.title.y + blacklist.title.height + 8
              <= blacklist.fullscreenCheckbox.y,
          "blacklist checkbox follows the title gap");
    Check(blacklist.fullscreenCheckbox.y + blacklist.fullscreenCheckbox.height
              < blacklist.list.y,
          "blacklist list follows the checkbox");
    Check(blacklist.list.y + blacklist.list.height
              + info_window_layout_detail::kBlacklistFooterGap
              <= blacklist.runningButton.y,
          "blacklist list stays above the footer");
    Check(blacklist.runningButton.y == blacklist.closeButton.y,
          "blacklist footer buttons share a baseline");
    Check(IsInside(blacklist.closeButton, 640, blacklistMinimumClientHeight),
          "blacklist close button stays inside the minimum client");

    const MainWindowHeaderLayout mainHeader =
        CalculateMainWindowHeaderLayout(100);
    Check(mainHeader.titleY == 106,
          "main title keeps its top inset");
    Check(mainHeader.titleHeight == 40,
          "main title leaves room for descenders");
    Check(mainHeader.hintY == 152,
          "main hint follows the expanded title");
    Check(mainHeader.hintHeight == 48,
          "main hint height remains unchanged");
    Check(mainHeader.listTop == 208,
          "main list preserves its gap below the shifted hint");
    Check(mainHeader.titleY + mainHeader.titleHeight
              <= mainHeader.hintY,
          "main title does not overlap hint");
    Check(mainHeader.hintY + mainHeader.hintHeight
              <= mainHeader.listTop,
          "main hint does not overlap list");

    Check(content_surface_style::kCornerRadius == 10,
          "main and Logs lists share the rounded corner radius");
    Check(content_surface_style::kRoundedListContentPadding == 6,
          "main and Logs lists share six-pixel content padding");
    Check(content_surface_style::kRoundedListRegionInset == 1,
          "main and Logs list children share a one-pixel region inset");
    Check(content_surface_style::kRoundedListRegionInset
              == content_surface_style::kDefaultRegionInset,
          "rounded list geometry does not add a second inset");
    Check(content_surface_style::kMessageFill == RGB(42, 42, 44),
          "message surface uses the approved soft fill");
    Check(content_surface_style::kMessageBorder == RGB(55, 55, 58),
          "message surface uses the approved soft border");
    Check(content_surface_style::kListFill == RGB(37, 37, 37),
          "existing list fill remains unchanged");
    constexpr DWORD baseStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP
        | WS_VSCROLL | LVS_REPORT | LVS_SHOWSELALWAYS;
    Check(content_surface_style::kApplicationTableWindowStyle == baseStyle,
          "blacklist and running picker use the same ListView window style");
    Check((content_surface_style::kApplicationTableWindowStyle & LVS_SINGLESEL) == 0,
          "application tables allow multiple selected rows");
    Check(content_surface_style::kApplicationTableExtendedStyle
              == (LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP),
          "blacklist and running picker use the same ListView extended style");
    Check(content_surface_style::StripListViewFrameStyle(baseStyle | WS_BORDER)
              == baseStyle,
          "table style stripping removes only WS_BORDER");
    Check(content_surface_style::StripListViewFrameStyle(baseStyle) == baseStyle,
          "table style stripping is idempotent");

    constexpr DWORD baseExStyle = WS_EX_NOPARENTNOTIFY | WS_EX_CONTROLPARENT;
    Check(content_surface_style::StripListViewFrameExStyle(
              baseExStyle | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)
              == baseExStyle,
          "table ex-style stripping removes only native frame edges");
    Check(content_surface_style::StripListViewFrameExStyle(baseExStyle)
              == baseExStyle,
          "table ex-style stripping is idempotent");

    const auto normalRow = content_surface_style::ResolveListRowVisual(false);
    Check(normalRow.fill == RGB(37, 37, 37)
              && normalRow.text == RGB(245, 245, 245),
          "normal table rows use the approved dark palette");
    const auto selectedRow = content_surface_style::ResolveListRowVisual(true);
    Check(selectedRow.fill == RGB(35, 105, 68)
              && selectedRow.text == RGB(255, 255, 255),
          "selected table rows use the approved green palette");

    constexpr UINT selectedHotFocused = CDIS_SELECTED | CDIS_HOT | CDIS_FOCUS;
    const auto selectedPaint = content_surface_style::ResolveListRowPaint(
        selectedHotFocused, true);
    Check((selectedPaint.itemState & (CDIS_SELECTED | CDIS_HOT)) == 0,
          "custom draw suppresses native selected and hot overlays");
    Check((selectedPaint.itemState & CDIS_FOCUS) != 0,
          "custom draw preserves keyboard focus indication");
    Check(content_surface_style::kTableRegionInset == 2,
          "tables expose a two-pixel rounded frame");
    Check(!content_surface_style::UsesNativeTableHeaderTheme(),
          "table headers use custom non-themed painting");
    Check(content_surface_style::kDefaultRegionInset == 1,
          "shared rounded controls keep their existing inset");

    const content_surface_style::SurfaceRect outerSurface = {0, 0, 100, 80};
    const auto insetSurface = content_surface_style::InsetSurfaceRect(
        outerSurface, content_surface_style::kRoundedListContentPadding);
    Check(insetSurface.x == 6 && insetSurface.y == 6
              && insetSurface.width == 88 && insetSurface.height == 68,
          "rounded list child uses one six-pixel outer padding");
    const auto tinySurface = content_surface_style::InsetSurfaceRect(
        content_surface_style::SurfaceRect{0, 0, 3, 2},
        content_surface_style::kRoundedListContentPadding);
    Check(tinySurface.width == 0 && tinySurface.height == 0,
          "tiny inset surfaces clamp dimensions to zero");

    constexpr content_surface_style::ApplicationTableColumn blacklistColumns[] = {
        {L"application_blacklist.column.application", 190},
        {L"application_blacklist.column.path", 500},
    };
    Check(std::wstring(blacklistColumns[0].title)
              == L"application_blacklist.column.application"
              && blacklistColumns[0].width == 190,
          "blacklist table keeps application column first");
    Check(std::wstring(blacklistColumns[1].title)
              == L"application_blacklist.column.path"
              && blacklistColumns[1].width == 500,
          "blacklist table keeps path column second");

    constexpr content_surface_style::ApplicationTableColumn runningColumns[] = {
        {L"application_blacklist.column.application", 170},
        {L"application_blacklist.column.window_title", 280},
        {L"application_blacklist.column.path", 520},
    };
    Check(std::wstring(runningColumns[0].title)
              == L"application_blacklist.column.application"
              && runningColumns[0].width == 170,
          "running picker keeps application column first");
    Check(std::wstring(runningColumns[1].title)
              == L"application_blacklist.column.window_title"
              && runningColumns[1].width == 280,
          "running picker keeps window title column second");
    Check(std::wstring(runningColumns[2].title)
              == L"application_blacklist.column.path"
              && runningColumns[2].width == 520,
          "running picker keeps path column third");

    const RunningPickerWindowLayout runningPicker =
        CalculateRunningPickerWindowLayout(760, 520);
    Check(runningPicker.title.height == 40,
          "running picker title leaves room for the large heading");
    Check(runningPicker.list.y
              >= runningPicker.title.y + runningPicker.title.height + 8,
          "running picker list follows the approved title gap");
    Check(runningPicker.list.y + runningPicker.list.height + 10
              <= runningPicker.primaryButton.y,
          "running picker list stays above the footer");
    Check(runningPicker.primaryButton.y == runningPicker.secondaryButton.y,
          "running picker buttons share a baseline");
    Check(IsInside(runningPicker.title, 760, 520),
          "running picker title stays inside the client");
    Check(IsInside(runningPicker.list, 760, 520),
          "running picker list stays inside the client");
    Check(IsInside(runningPicker.primaryButton, 760, 520),
          "running picker primary button stays inside the client");
    Check(IsInside(runningPicker.secondaryButton, 760, 520),
          "running picker secondary button stays inside the client");
    Check(info_window_layout_detail::kMessageCompactTitleHeight == 24,
          "generic message title height remains compact");
    Check(info_window_layout_detail::kMessageCompactTitleGap == 6,
          "generic message title gap remains unchanged");
    return 0;
}
