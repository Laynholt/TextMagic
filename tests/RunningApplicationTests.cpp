#include "RunningApplication.h"

#include <cassert>
#include <vector>

int main() {
    const std::vector<RunningApplication> applications = {
        { L"Alpha.exe", L"First Alpha Window", L"C:\\Apps\\Alpha.exe" },
        { L"Empty.exe", L"Missing Path", L"" },
        { L"ALPHA.EXE", L"Duplicate Alpha Window", L"c:\\apps\\ALPHA.EXE" },
        { L"Beta.exe", L"Beta Window", L"D:\\Tools\\Beta.exe" }
    };

    const std::vector<RunningApplication> deduplicated =
        DeduplicateRunningApplications(applications);

    assert(deduplicated.size() == 2);
    assert(deduplicated[0].executableName == L"Alpha.exe");
    assert(deduplicated[0].windowTitle == L"First Alpha Window");
    assert(deduplicated[0].path == L"C:\\Apps\\Alpha.exe");
    assert(ApplicationBlacklist::PathsEqual(
        deduplicated[0].path,
        L"c:\\apps\\alpha.exe"
    ));
    assert(deduplicated[1].executableName == L"Beta.exe");
    assert(deduplicated[1].windowTitle == L"Beta Window");
    assert(ApplicationBlacklist::PathsEqual(
        deduplicated[1].path,
        L"D:\\TOOLS\\BETA.EXE"
    ));

    return 0;
}
