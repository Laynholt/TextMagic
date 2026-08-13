#pragma once

#include <cstdint>
#include <string>

struct ArchiveImportLimits {
    std::uint64_t maxEntries = 0;
    std::uint64_t maxFileBytes = 0;
    std::uint64_t maxTotalBytes = 0;
};

struct ArchiveImportMessages {
    std::wstring entryLimitExceeded;
    std::wstring fileLimitExceeded;
    std::wstring totalLimitExceeded;
};

ArchiveImportLimits DefaultArchiveImportLimits() noexcept;

std::wstring BuildArchivePreflightAndExtractScript(
    const std::wstring& archivePath,
    const std::wstring& destinationPath,
    const ArchiveImportLimits& limits,
    const ArchiveImportMessages& messages);
