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
    InfoRect identityPanel;
    InfoRect detailsPanel;
    InfoRect pathValue;
    InfoRect hint;
    InfoRect actionButton;
    InfoRect closeButton;
};

namespace info_window_layout_detail {
constexpr int kOuterInset = 16;
constexpr int kContentInset = 14;
constexpr int kTitleHeight = 28;
constexpr int kSubtitleHeight = 20;
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
constexpr int kAboutPanelMinimumHeight = 110;
constexpr int kAboutDescriptionHeight = 40;
constexpr int kAboutIdentityHeight = 64;
constexpr int kAboutDetailsHeight = 132;
constexpr int kAboutPathHeight = 40;
constexpr int kAboutCompactGap = 1;

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

    const int normalDetailsY = title.y + title.height
        + info_window_layout_detail::kContentInset
        + info_window_layout_detail::kAboutDescriptionHeight
        + info_window_layout_detail::kContentInset
        + info_window_layout_detail::kAboutIdentityHeight
        + info_window_layout_detail::kContentInset;
    const int normalDetailsHeight = std::max(
        info_window_layout_detail::kAboutPanelMinimumHeight,
        info_window_layout_detail::kAboutDetailsHeight);
    const int normalHintBottom = normalDetailsY + normalDetailsHeight
        + info_window_layout_detail::kContentInset
        + info_window_layout_detail::kSubtitleHeight;
    const int anchoredFooterY = info_window_layout_detail::FooterY(clientHeight);
    const bool compact = anchoredFooterY < normalHintBottom;
    const int sectionGap = compact
        ? info_window_layout_detail::kAboutCompactGap
        : info_window_layout_detail::kContentInset;
    const int footerY = compact
        ? std::max(
            info_window_layout_detail::kOuterInset,
            info_window_layout_detail::NonNegative(clientHeight)
                - info_window_layout_detail::kButtonHeight)
        : anchoredFooterY;

    const InfoRect description{
        contentX,
        title.y + title.height + sectionGap,
        contentWidth,
        info_window_layout_detail::kAboutDescriptionHeight,
    };
    const InfoRect identityPanel{
        contentX,
        description.y + description.height + sectionGap,
        contentWidth,
        info_window_layout_detail::kAboutIdentityHeight,
    };
    const int detailsY = identityPanel.y + identityPanel.height + sectionGap;
    const int detailsHeight = compact
        ? std::max(
            info_window_layout_detail::kAboutPanelMinimumHeight,
            std::min(
                info_window_layout_detail::kAboutDetailsHeight,
                footerY - detailsY))
        : normalDetailsHeight;
    const InfoRect detailsPanel{
        contentX,
        detailsY,
        contentWidth,
        detailsHeight,
    };
    const InfoRect pathValue{
        detailsPanel.x + info_window_layout_detail::kContentInset,
        detailsPanel.y + info_window_layout_detail::kContentInset,
        info_window_layout_detail::NonNegative(
            detailsPanel.width - 2 * info_window_layout_detail::kContentInset),
        info_window_layout_detail::kAboutPathHeight,
    };
    const InfoRect hint{
        contentX,
        compact
            ? detailsPanel.y + detailsPanel.height
                - info_window_layout_detail::kSubtitleHeight
            : detailsPanel.y + detailsPanel.height
                + info_window_layout_detail::kContentInset,
        contentWidth,
        info_window_layout_detail::kSubtitleHeight,
    };

    constexpr int actionWidth = 210;
    constexpr int closeWidth = 140;
    const int rightEdge = info_window_layout_detail::ClientWidth(clientWidth)
        - info_window_layout_detail::kOuterInset;
    const InfoRect closeButton = info_window_layout_detail::RightAlignedButton(
        clientWidth, footerY, closeWidth, rightEdge);
    const int actionRightEdge = closeButton.x - info_window_layout_detail::kButtonGap;
    const InfoRect actionButton = info_window_layout_detail::RightAlignedButton(
        clientWidth, footerY, actionWidth, actionRightEdge);

    return {
        title,
        description,
        identityPanel,
        detailsPanel,
        pathValue,
        hint,
        actionButton,
        closeButton,
    };
}
