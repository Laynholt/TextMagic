#include "RunningApplication.h"
#include "../src/app/ContentSurfaceStyle.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}
}

int main() {
    Expect(content_surface_style::ResolveTableFramePaintMode()
               == content_surface_style::TableFramePaintMode::StrokeOnly,
        "running picker uses the shared stroke-only table frame");

    const std::vector<RunningApplication> applications = {
        { L"Alpha.exe", L"First Alpha Window", L"C:\\Apps\\Alpha.exe" },
        { L"Empty.exe", L"Missing Path", L"" },
        { L"ALPHA.EXE", L"Duplicate Alpha Window", L"c:\\apps\\ALPHA.EXE" },
        { L"Beta.exe", L"Beta Window", L"D:\\Tools\\Beta.exe" }
    };

    const std::vector<RunningApplication> deduplicated =
        DeduplicateRunningApplications(applications);

    Expect(deduplicated.size() == 2,
        "empty paths and case-variant duplicates must be skipped");
    Expect(deduplicated[0].executableName == L"Alpha.exe",
        "first application name and order must be preserved");
    Expect(deduplicated[0].windowTitle == L"First Alpha Window",
        "first application title must be preserved");
    Expect(deduplicated[0].path == L"C:\\Apps\\Alpha.exe",
        "first application path must be preserved");
    Expect(ApplicationBlacklist::PathsEqual(
        deduplicated[0].path,
        L"c:\\apps\\alpha.exe"
    ), "same-path case variants must compare equal");
    Expect(deduplicated[1].executableName == L"Beta.exe",
        "second application name and order must be preserved");
    Expect(deduplicated[1].windowTitle == L"Beta Window",
        "second application title must be preserved");
    Expect(deduplicated[1].path == L"D:\\Tools\\Beta.exe",
        "second application path must be preserved");
    Expect(ApplicationBlacklist::PathsEqual(
        deduplicated[1].path,
        L"D:\\TOOLS\\BETA.EXE"
    ), "deduplicated paths must retain ordinal case-insensitive comparison");

    const RunningApplication alpha = {
        L"Alpha.exe", L"Zulu window", L"C:\\Apps\\Alpha.exe"
    };
    const RunningApplication beta = {
        L"beta.exe", L"alpha window", L"D:\\Tools\\beta.exe"
    };

    Expect(CompareRunningApplications(
        alpha, beta, RunningApplicationColumn::ExecutableName
    ) < 0, "application-name comparison must be case-insensitive ascending");
    Expect(CompareRunningApplications(
        alpha, beta, RunningApplicationColumn::WindowTitle
    ) > 0, "window-title comparison must use the selected column");
    Expect(CompareRunningApplications(
        alpha, beta, RunningApplicationColumn::Path
    ) < 0, "path comparison must use the selected column");
    Expect(CompareRunningApplications(
        beta, alpha, RunningApplicationColumn::ExecutableName
    ) > 0, "reversed arguments must support descending sorting");
    Expect(CompareRunningApplications(
        alpha, { L"ALPHA.EXE", L"ignored", L"ignored" },
        RunningApplicationColumn::ExecutableName
    ) == 0, "case variants must compare equal");

    return 0;
}
