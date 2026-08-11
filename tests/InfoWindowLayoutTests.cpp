#include "InfoWindowLayout.h"

#include <cstdlib>
#include <iostream>

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
    return 0;
}
