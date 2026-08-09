#include "UpdateService.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}
}

int main() {
    const std::filesystem::path invalidPath =
        std::filesystem::temp_directory_path() / L"TextMagic-invalid-update.exe";
    {
        std::ofstream output(invalidPath, std::ios::binary | std::ios::trunc);
        output << "not an executable";
    }

    std::wstring error;
    bool passed = Check(
        !UpdateService::VerifyExecutableTrust(invalidPath.wstring(), error),
        "unsigned invalid update is rejected"
    );
    passed &= Check(!error.empty(), "rejected update reports an error");

    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT systemDirectoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    error.clear();
    passed &= Check(systemDirectoryLength > 0 && systemDirectoryLength < MAX_PATH,
                    "system directory is available");
    passed &= Check(
        UpdateService::VerifyExecutableTrust(
            (std::filesystem::path(systemDirectory) / L"kernel32.dll").wstring(),
            error
        ),
        "trusted Windows binary is accepted"
    );

    const std::filesystem::path updateDirectory =
        std::filesystem::temp_directory_path() / L"TextMagic-updater-rollback-test";
    const std::filesystem::path downloadPath = updateDirectory / L"download.exe";
    const std::filesystem::path targetPath = updateDirectory / L"target.exe";
    const std::filesystem::path backupPath = targetPath.wstring() + L".bak";
    std::error_code fileError;
    std::filesystem::remove_all(updateDirectory, fileError);
    std::filesystem::create_directories(updateDirectory, fileError);
    std::filesystem::copy_file(
        std::filesystem::path(systemDirectory) / L"kernel32.dll",
        downloadPath,
        std::filesystem::copy_options::overwrite_existing,
        fileError
    );
    {
        std::ofstream target(targetPath, std::ios::binary | std::ios::trunc);
        target << "original";
    }

    UpdateService service;
    error.clear();
    passed &= Check(
        service.LaunchUpdaterProcess(
            2147483647,
            downloadPath.wstring(),
            targetPath.wstring(),
            error
        ),
        "rollback updater process starts"
    );
    for (int attempt = 0;
         attempt < 100
            && (std::filesystem::exists(downloadPath) || std::filesystem::exists(backupPath));
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::ifstream restoredTarget(targetPath, std::ios::binary);
    const std::string restoredContents{
        std::istreambuf_iterator<char>(restoredTarget),
        std::istreambuf_iterator<char>()
    };
    passed &= Check(!std::filesystem::exists(downloadPath), "failed update is consumed");
    passed &= Check(!std::filesystem::exists(backupPath), "rollback backup is cleaned up");
    passed &= Check(restoredContents == "original", "failed startup restores the original executable");

    std::filesystem::remove(invalidPath, fileError);
    std::filesystem::remove_all(updateDirectory, fileError);
    return passed ? 0 : 1;
}
