#pragma once

#include <algorithm>

struct InfoRect {
    int x;
    int y;
    int width;
    int height;
};

struct LogsWindowLayout {
    InfoRect title;
    InfoRect subtitle;
    InfoRect content;
    InfoRect copyAllButton;
    InfoRect closeButton;
};

struct AboutWindowLayout {
    InfoRect title;
    InfoRect description;
    InfoRect versionLine;
    InfoRect detailsPanel;
    InfoRect loadedScriptsLabel;
    InfoRect loadedScriptsValue;
    InfoRect divider;
    InfoRect directoryLabel;
    InfoRect directoryValue;
    InfoRect hint;
    InfoRect actionButton;
    InfoRect closeButton;
};

namespace info_window_layout_detail {
constexpr int kOuterInset = 16;
constexpr int kContentInset = 14;
constexpr int kTitleHeight = 32;
constexpr int kSubtitleHeight = 18;
constexpr int kButtonHeight = 36;
constexpr int kFooterGap = 14;
constexpr int kButtonGap = 10;

constexpr int kLogsContentMinimumHeight = 80;
constexpr int kLogsMinimumClientHeight = kOuterInset
    + kTitleHeight
    + kContentInset
    + kSubtitleHeight
    + kContentInset
    + kLogsContentMinimumHeight
    + kFooterGap
    + kOuterInset
    + kButtonHeight;
constexpr int kAboutMinimumClientHeight = 400;
constexpr int kAboutDescriptionHeight = 40;
constexpr int kAboutVersionLineHeight = 20;
constexpr int kAboutDetailsHeight = 160;
constexpr int kAboutRowLabelHeight = 18;
constexpr int kAboutValueHeight = 24;
constexpr int kAboutPathHeight = 40;
constexpr int kAboutDividerHeight = 1;

inline int NonNegative(int value) {
    return std::max(0, value);
}

inline int ClientWidth(int width) {
    return NonNegative(width);
}

inline int OuterWidth(int clientWidth) {
    return NonNegative(ClientWidth(clientWidth) - 2 * kOuterInset);
}

inline int ContentWidth(int clientWidth) {
    return NonNegative(OuterWidth(clientWidth) - 2 * kContentInset);
}

inline int FooterY(int clientHeight) {
    return std::max(kOuterInset,
                    NonNegative(clientHeight) - kOuterInset - kButtonHeight);
}

inline InfoRect RightAlignedButton(int clientWidth,
                                   int y,
                                   int desiredWidth,
                                   int rightEdge) {
    const int safeRightEdge = std::min(ClientWidth(clientWidth),
                                       std::max(0, rightEdge));
    const int availableWidth = NonNegative(safeRightEdge - kOuterInset);
    const int width = std::min(desiredWidth, availableWidth);
    return {safeRightEdge - width, y, width, kButtonHeight};
}
}

inline LogsWindowLayout CalculateLogsWindowLayout(int clientWidth, int clientHeight) {
    const int outerWidth = info_window_layout_detail::OuterWidth(clientWidth);
    const int contentWidth = info_window_layout_detail::ContentWidth(clientWidth);
    const int contentX = info_window_layout_detail::kOuterInset
        + info_window_layout_detail::kContentInset;

    const InfoRect title{
        info_window_layout_detail::kOuterInset,
        info_window_layout_detail::kOuterInset,
        outerWidth,
        info_window_layout_detail::kTitleHeight,
    };
    const InfoRect subtitle{
        title.x,
        title.y + title.height + info_window_layout_detail::kContentInset,
        title.width,
        info_window_layout_detail::kSubtitleHeight,
    };
    const int footerY = info_window_layout_detail::FooterY(std::max(
        info_window_layout_detail::NonNegative(clientHeight),
        info_window_layout_detail::kLogsMinimumClientHeight));
    const int contentY = subtitle.y + subtitle.height
        + info_window_layout_detail::kContentInset;
    const int availableContentHeight = footerY - info_window_layout_detail::kFooterGap - contentY;
    const InfoRect content{
        contentX,
        contentY,
        contentWidth,
        std::max(info_window_layout_detail::kLogsContentMinimumHeight,
                 availableContentHeight),
    };

    constexpr int copyAllWidth = 180;
    constexpr int closeWidth = 140;
    const int rightEdge = info_window_layout_detail::ClientWidth(clientWidth)
        - info_window_layout_detail::kOuterInset;
    const InfoRect closeButton = info_window_layout_detail::RightAlignedButton(
        clientWidth, footerY, closeWidth, rightEdge);
    const int copyRightEdge = closeButton.x - info_window_layout_detail::kButtonGap;
    const InfoRect copyAllButton = info_window_layout_detail::RightAlignedButton(
        clientWidth, footerY, copyAllWidth, copyRightEdge);

    return {title, subtitle, content, copyAllButton, closeButton};
}

inline AboutWindowLayout CalculateAboutWindowLayout(int clientWidth, int clientHeight) {
    const int outerWidth = info_window_layout_detail::OuterWidth(clientWidth);
    const int contentWidth = info_window_layout_detail::ContentWidth(clientWidth);
    const int contentX = info_window_layout_detail::kOuterInset
        + info_window_layout_detail::kContentInset;

    const InfoRect title{
        info_window_layout_detail::kOuterInset,
        info_window_layout_detail::kOuterInset,
        outerWidth,
        info_window_layout_detail::kTitleHeight,
    };
    const InfoRect description{
        contentX,
        title.y + title.height + info_window_layout_detail::kContentInset,
        contentWidth,
        info_window_layout_detail::kAboutDescriptionHeight,
    };
    const InfoRect versionLine{
        contentX,
        description.y + description.height + info_window_layout_detail::kContentInset,
        contentWidth,
        info_window_layout_detail::kAboutVersionLineHeight,
    };
    const InfoRect detailsPanel{
        contentX,
        versionLine.y + versionLine.height + info_window_layout_detail::kContentInset,
        contentWidth,
        info_window_layout_detail::kAboutDetailsHeight,
    };

    const int detailsInset = info_window_layout_detail::kContentInset;
    const int detailsX = detailsPanel.x + detailsInset;
    const int detailsWidth = info_window_layout_detail::NonNegative(
        detailsPanel.width - 2 * detailsInset);
    const int rowGap = 4;
    const int dividerGap = 10;
    const int loadedScriptsLabelY = detailsPanel.y + detailsInset;
    const InfoRect loadedScriptsLabel{
        detailsX,
        loadedScriptsLabelY,
        detailsWidth,
        info_window_layout_detail::kAboutRowLabelHeight,
    };
    const InfoRect loadedScriptsValue{
        detailsX,
        loadedScriptsLabel.y + loadedScriptsLabel.height + rowGap,
        detailsWidth,
        info_window_layout_detail::kAboutValueHeight,
    };
    const InfoRect divider{
        detailsX,
        loadedScriptsValue.y + loadedScriptsValue.height + dividerGap,
        detailsWidth,
        info_window_layout_detail::kAboutDividerHeight,
    };
    const InfoRect directoryLabel{
        detailsX,
        divider.y + divider.height + dividerGap,
        detailsWidth,
        info_window_layout_detail::kAboutRowLabelHeight,
    };
    const InfoRect directoryValue{
        detailsX,
        directoryLabel.y + directoryLabel.height + rowGap,
        detailsWidth,
        info_window_layout_detail::kAboutPathHeight,
    };
    const int effectiveClientHeight = std::max(
        info_window_layout_detail::NonNegative(clientHeight),
        info_window_layout_detail::kAboutMinimumClientHeight);
    const int footerY = info_window_layout_detail::FooterY(effectiveClientHeight);
    const InfoRect hint{
        contentX,
        detailsPanel.y + detailsPanel.height + info_window_layout_detail::kContentInset,
        contentWidth,
        info_window_layout_detail::kSubtitleHeight,
    };

    constexpr int actionWidth = 210;
    constexpr int closeWidth = 140;
    const int rightEdge = info_window_layout_detail::ClientWidth(clientWidth)
        - info_window_layout_detail::kOuterInset;
    const InfoRect closeButton = info_window_layout_detail::RightAlignedButton(
        clientWidth, footerY, closeWidth, rightEdge);
    const int actionAvailableWidth = info_window_layout_detail::NonNegative(
        closeButton.x - info_window_layout_detail::kButtonGap
            - info_window_layout_detail::kOuterInset);
    const InfoRect actionButton{
        info_window_layout_detail::kOuterInset,
        footerY,
        std::min(actionWidth, actionAvailableWidth),
        info_window_layout_detail::kButtonHeight,
    };

    return {
        title,
        description,
        versionLine,
        detailsPanel,
        loadedScriptsLabel,
        loadedScriptsValue,
        divider,
        directoryLabel,
        directoryValue,
        hint,
        actionButton,
        closeButton,
    };
}
