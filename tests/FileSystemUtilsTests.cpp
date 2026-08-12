#include "FileSystemUtils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}
}

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / L"TextMagic-file-system-test";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root / L"nested", cleanupError);

    std::ofstream(root / L"top.tmscript") << "top";
    std::ofstream(root / L"nested" / L"child.tmscript") << "child";

    std::vector<std::filesystem::path> files;
    std::error_code error;
    bool passed = Check(
        FileSystemUtils::CollectRegularFiles(root, true, &files, &error),
        "recursive file collection succeeds"
    );
    passed &= Check(error.value() == 0, "successful collection has no error");
    passed &= Check(files.size() == 2, "recursive collection includes nested files");

    error.clear();
    passed &= Check(
        !FileSystemUtils::CollectRegularFiles(root / L"missing", false, &files, &error),
        "missing directory collection fails"
    );
    passed &= Check(error.value() != 0, "missing directory reports an error");

    error.clear();
    const std::wstring tempDirectory = FileSystemUtils::GetTempDirectory(&error);
    passed &= Check(!tempDirectory.empty(), "temporary directory path is available");
    passed &= Check(error.value() == 0, "temporary directory lookup has no error");

    error.clear();
    const std::wstring modulePath = FileSystemUtils::GetModulePath(nullptr, &error);
    passed &= Check(!modulePath.empty(), "module path is available");
    passed &= Check(error.value() == 0, "module path lookup has no error");

    std::filesystem::remove_all(root, cleanupError);
    return passed ? 0 : 1;
}
