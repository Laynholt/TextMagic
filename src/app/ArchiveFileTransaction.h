#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

class ScopedPathCleanup {
public:
    explicit ScopedPathCleanup(std::filesystem::path path);
    ~ScopedPathCleanup();

    ScopedPathCleanup(const ScopedPathCleanup&) = delete;
    ScopedPathCleanup& operator=(const ScopedPathCleanup&) = delete;

    bool Cleanup(std::error_code& error) noexcept;
    void Release() noexcept;

private:
    std::filesystem::path m_path;
    bool m_active = true;
};

using ArchiveCopyOperation = std::function<bool(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::error_code& error)>;

using ArchiveRemoveOperation = std::function<bool(
    const std::filesystem::path& path,
    std::error_code& error)>;

using ArchiveProducer =
    std::function<bool(const std::filesystem::path& stagingArchive)>;

enum class ArchiveExportFailure {
    None,
    PrepareStaging,
    Produce,
    Commit,
};

struct ArchiveImportResult {
    bool success = false;
    std::error_code error;
    std::vector<std::filesystem::path> importedPaths;
    std::vector<std::filesystem::path> rollbackFailedPaths;
};

ArchiveImportResult ImportArchiveFilesTransactionally(
    const std::filesystem::path& destinationDirectory,
    const std::vector<std::filesystem::path>& sourceFiles,
    const ArchiveCopyOperation& copyOperation = {},
    const ArchiveRemoveOperation& removeOperation = {});

std::filesystem::path CreateUniqueTemporaryDirectory(
    const std::filesystem::path& parentDirectory,
    const std::wstring& prefix,
    std::error_code& error);

std::filesystem::path CreateSiblingStagingArchivePath(
    const std::filesystem::path& targetArchive,
    std::error_code& error);

bool CommitStagedArchive(
    const std::filesystem::path& stagedArchive,
    const std::filesystem::path& targetArchive,
    std::error_code& error);

bool ExportArchiveTransactionally(
    const std::filesystem::path& targetArchive,
    const ArchiveProducer& producer,
    std::error_code& error,
    ArchiveExportFailure* failure = nullptr);
