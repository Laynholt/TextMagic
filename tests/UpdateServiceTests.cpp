#include "UpdateService.h"

#include <bcrypt.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool ComputeSha256Fixture(const std::filesystem::path& filePath, std::string& hashHex) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD bytesReturned = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0
    );
    if (status >= 0) {
        status = BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &bytesReturned, 0
        );
    }
    std::vector<UCHAR> hashObject(objectLength);
    if (status >= 0) {
        status = BCryptCreateHash(
            algorithm, &hash, hashObject.data(), objectLength,
            nullptr, 0, 0
        );
    }

    std::array<char, 64 * 1024> buffer = {};
    while (status >= 0 && input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytesRead = input.gcount();
        if (bytesRead > 0) {
            status = BCryptHashData(
                hash, reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(bytesRead), 0
            );
        }
    }

    std::array<UCHAR, 32> digest = {};
    if (status >= 0) {
        status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (status < 0) {
        return false;
    }

    constexpr char digits[] = "0123456789abcdef";
    hashHex.clear();
    hashHex.reserve(digest.size() * 2);
    for (UCHAR byte : digest) {
        hashHex.push_back(digits[byte >> 4]);
        hashHex.push_back(digits[byte & 0x0f]);
    }
    return true;
}

bool WriteChecksumFixture(const std::filesystem::path& filePath,
                          const std::filesystem::path& sumsPath,
                          const char* entryName) {
    std::string hashHex;
    if (!ComputeSha256Fixture(filePath, hashHex)) {
        return false;
    }
    std::ofstream output(sumsPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << hashHex << "  " << entryName << "\r\n";
    return static_cast<bool>(output);
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
    bool passed = false;
    {
        std::ofstream output(checksumFile, std::ios::binary | std::ios::trunc);
        output << "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  "
                  "TextMagic.exe\r\n";
        passed = Check(static_cast<bool>(output), "test fixture writes a checksum entry");
    }
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
    passed &= Check(WriteChecksumFixture(downloadPath, downloadSums, "TextMagic.exe"),
                    "download checksum fixture is generated for updater test");
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
    passed &= Check(WriteChecksumFixture(downloadPath, downloadSums, "TextMagic.exe"),
                    "tamper test checksum fixture is generated");
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
