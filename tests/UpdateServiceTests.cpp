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
    const std::filesystem::path checksumDirectory =
        std::filesystem::temp_directory_path() / L"TextMagic-checksum-test";
    const std::filesystem::path checksumTarget = checksumDirectory / L"textmagic.exe";
    const std::filesystem::path checksumFile = checksumDirectory / L"SHA256SUMS.txt";
    std::error_code fileError;
    std::filesystem::remove_all(checksumDirectory, fileError);
    std::filesystem::create_directories(checksumDirectory, fileError);
    {
        std::ofstream output(checksumTarget, std::ios::binary | std::ios::trunc);
        output << "abc";
    }

    std::wstring error;
    bool passed = Check(UpdateService::WriteSha256SumsFile(
                            checksumTarget.wstring(), checksumFile.wstring(), error),
                        "SHA256SUMS.txt is generated");
    std::ifstream generatedChecksum(checksumFile, std::ios::binary);
    const std::string checksumContents{
        std::istreambuf_iterator<char>(generatedChecksum),
        std::istreambuf_iterator<char>()
    };
    passed &= Check(
        checksumContents ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  TextMagic.exe\r\n",
        "generated checksum uses the conventional SHA256SUMS format"
    );
    error.clear();
    passed &= Check(UpdateService::VerifySha256SumsFile(
                        checksumTarget.wstring(), checksumFile.wstring(), L"TextMagic.exe", error),
                    "matching executable checksum is accepted");

    {
        std::ofstream output(checksumTarget, std::ios::binary | std::ios::trunc);
        output << "changed";
    }
    error.clear();
    passed &= Check(!UpdateService::VerifySha256SumsFile(
                        checksumTarget.wstring(), checksumFile.wstring(), L"TextMagic.exe", error),
                    "changed executable is rejected");
    passed &= Check(!error.empty(), "checksum mismatch reports an error");

    {
        std::ofstream output(checksumFile, std::ios::binary | std::ios::trunc);
        output << "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  Other.exe\r\n";
    }
    error.clear();
    passed &= Check(!UpdateService::VerifySha256SumsFile(
                        checksumTarget.wstring(), checksumFile.wstring(), L"TextMagic.exe", error),
                    "checksum entry for another asset is rejected");

    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT systemDirectoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    error.clear();
    passed &= Check(systemDirectoryLength > 0 && systemDirectoryLength < MAX_PATH,
                    "system directory is available");

    const std::filesystem::path updateDirectory =
        std::filesystem::temp_directory_path() / L"TextMagic-updater-rollback-test";
    const std::filesystem::path downloadPath = updateDirectory / L"download.exe";
    const std::filesystem::path targetPath = updateDirectory / L"target.exe";
    const std::filesystem::path backupPath = targetPath.wstring() + L".bak";
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
    const std::filesystem::path downloadSums = updateDirectory / L"SHA256SUMS.txt";
    std::wstring expectedDownloadHash;
    passed &= Check(UpdateService::WriteSha256SumsFile(
                        downloadPath.wstring(), downloadSums.wstring(), error),
                    "download checksum is generated for updater test");
    passed &= Check(UpdateService::VerifySha256SumsFile(
                        downloadPath.wstring(), downloadSums.wstring(), L"TextMagic.exe",
                        error, &expectedDownloadHash),
                    "updater test obtains the verified checksum");
    error.clear();
    passed &= Check(
        service.LaunchUpdaterProcess(
            2147483647,
            downloadPath.wstring(),
            targetPath.wstring(),
            expectedDownloadHash,
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
    error.clear();
    passed &= Check(UpdateService::WriteSha256SumsFile(
                        downloadPath.wstring(), downloadSums.wstring(), error),
                    "tamper test checksum is generated");
    passed &= Check(UpdateService::VerifySha256SumsFile(
                        downloadPath.wstring(), downloadSums.wstring(), L"TextMagic.exe",
                        error, &expectedDownloadHash),
                    "tamper test obtains the verified checksum");

    STARTUPINFOW blockerStartup = {};
    blockerStartup.cb = sizeof(blockerStartup);
    PROCESS_INFORMATION blockerProcess = {};
    wchar_t blockerCommand[] = L"cmd.exe /d /c ping 127.0.0.1 -n 4 > nul";
    const bool blockerStarted = CreateProcessW(
        nullptr, blockerCommand, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &blockerStartup, &blockerProcess
    ) != FALSE;
    passed &= Check(blockerStarted, "tamper test wait process starts");
    if (blockerStarted) {
        error.clear();
        passed &= Check(service.LaunchUpdaterProcess(
                            blockerProcess.dwProcessId,
                            downloadPath.wstring(),
                            targetPath.wstring(),
                            expectedDownloadHash,
                            error),
                        "updater waits before installing verified download");
        {
            std::ofstream tampered(downloadPath, std::ios::binary | std::ios::trunc);
            tampered << "tampered";
        }
        WaitForSingleObject(blockerProcess.hProcess, 10000);
        CloseHandle(blockerProcess.hThread);
        CloseHandle(blockerProcess.hProcess);
        for (int attempt = 0; attempt < 100 && std::filesystem::exists(downloadPath); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::ifstream untouchedTarget(targetPath, std::ios::binary);
        const std::string untouchedContents{
            std::istreambuf_iterator<char>(untouchedTarget),
            std::istreambuf_iterator<char>()
        };
        passed &= Check(!std::filesystem::exists(downloadPath),
                        "tampered download is discarded at point of use");
        passed &= Check(untouchedContents == "original",
                        "tampered download does not replace the original executable");
    }

    std::filesystem::remove_all(checksumDirectory, fileError);
    std::filesystem::remove_all(updateDirectory, fileError);
    return passed ? 0 : 1;
}
