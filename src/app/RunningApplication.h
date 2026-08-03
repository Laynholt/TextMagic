#pragma once

#include "ApplicationBlacklist.h"

#include <algorithm>
#include <string>
#include <vector>

struct RunningApplication {
    std::wstring executableName;
    std::wstring windowTitle;
    std::wstring path;
};

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
