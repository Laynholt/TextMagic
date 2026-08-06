#pragma once

#include "ApplicationBlacklist.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

struct RunningApplication {
    std::wstring executableName;
    std::wstring windowTitle;
    std::wstring path;
};

enum class RunningApplicationColumn {
    ExecutableName = 0,
    WindowTitle = 1,
    Path = 2
};

inline int CompareRunningApplications(
    const RunningApplication& left,
    const RunningApplication& right,
    RunningApplicationColumn column
) noexcept {
    const std::wstring* leftValue = &left.executableName;
    const std::wstring* rightValue = &right.executableName;
    if (column == RunningApplicationColumn::WindowTitle) {
        leftValue = &left.windowTitle;
        rightValue = &right.windowTitle;
    } else if (column == RunningApplicationColumn::Path) {
        leftValue = &left.path;
        rightValue = &right.path;
    }

    const int result = CompareStringOrdinal(
        leftValue->c_str(), -1, rightValue->c_str(), -1, TRUE
    );
    if (result == CSTR_LESS_THAN) {
        return -1;
    }
    if (result == CSTR_GREATER_THAN) {
        return 1;
    }
    return 0;
}

inline std::vector<RunningApplication> DeduplicateRunningApplications(
    const std::vector<RunningApplication>& applications
) {
    std::vector<RunningApplication> result;
    for (const RunningApplication& application : applications) {
        if (application.path.empty()) {
            continue;
        }
        const auto duplicate = std::find_if(
            result.begin(),
            result.end(),
            [&application](const RunningApplication& existing) {
                return ApplicationBlacklist::PathsEqual(existing.path, application.path);
            }
        );
        if (duplicate == result.end()) {
            result.push_back(application);
        }
    }
    return result;
}
