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
    Check(about.identityPanel.y > about.description.y + about.description.height,
          "about identity follows description");
    Check(about.detailsPanel.y > about.identityPanel.y + about.identityPanel.height,
          "about details follow identity");
    Check(about.pathValue.y >= about.detailsPanel.y,
          "path value belongs to details panel");
    Check(about.actionButton.y == about.closeButton.y,
          "about buttons share a baseline");
    Check(IsInside(about.closeButton, 620, 440), "about close button stays inside");

    const AboutWindowLayout minimumAbout = CalculateAboutWindowLayout(620, 300);
    Check(IsInside(minimumAbout.title, 620, 300),
          "about minimum title stays inside");
    Check(IsInside(minimumAbout.description, 620, 300),
          "about minimum description stays inside");
    Check(IsInside(minimumAbout.identityPanel, 620, 300),
          "about minimum identity panel stays inside");
    Check(IsInside(minimumAbout.detailsPanel, 620, 300),
          "about minimum details panel stays inside");
    Check(IsInside(minimumAbout.pathValue, 620, 300),
          "about minimum path value stays inside");
    Check(IsInside(minimumAbout.hint, 620, 300),
          "about minimum hint stays inside");
    Check(IsInside(minimumAbout.actionButton, 620, 300),
          "about minimum action button stays inside");
    Check(IsInside(minimumAbout.closeButton, 620, 300),
          "about minimum close button stays inside");
    Check(minimumAbout.detailsPanel.y + minimumAbout.detailsPanel.height
              <= minimumAbout.actionButton.y,
          "about minimum details stay above footer");
    Check(minimumAbout.hint.y + minimumAbout.hint.height
              <= minimumAbout.actionButton.y,
          "about minimum hint stays above footer");
    return 0;
}
