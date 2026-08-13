#pragma once

#include <windows.h>

#include <string>

struct UpdateCheckResult {
    bool success;
    bool updateAvailable;
    std::wstring latestTag;
    std::wstring latestVersion;
    std::wstring errorMessage;
};

class UpdateService {
public:
    UpdateCheckResult CheckForUpdates(const std::wstring& currentVersion) const;
    bool DownloadReleaseExecutable(const std::wstring& tag,
                                   const std::wstring& destinationPath,
                                   std::wstring& verifiedSha256,
                                   std::wstring& errorMessage) const;
    bool LaunchUpdaterProcess(DWORD currentProcessId,
                              const std::wstring& downloadedExePath,
                              const std::wstring& targetExePath,
                              const std::wstring& expectedSha256,
                              std::wstring& errorMessage) const;
    static bool VerifySha256SumsFile(const std::wstring& filePath,
                                     const std::wstring& sumsPath,
                                     const std::wstring& entryName,
                                     std::wstring& errorMessage,
                                     std::wstring* verifiedSha256 = nullptr);
    static int CompareVersions(const std::wstring& left, const std::wstring& right);

private:
    bool DownloadReleaseAsset(const std::wstring& tag,
                              const std::wstring& assetName,
                              const std::wstring& destinationPath,
                              std::wstring& errorMessage) const;
    bool ResolveLatestReleaseTag(std::wstring& latestTag, std::wstring& errorMessage) const;

    static std::wstring NormalizeVersionFromTag(const std::wstring& rawTag);
};
