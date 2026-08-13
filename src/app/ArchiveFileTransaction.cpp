#include "ArchiveFileTransaction.h"

#include <windows.h>

#include <chrono>
#include <array>

namespace fs = std::filesystem;

namespace {
std::error_code LastWin32Error() {
    return {static_cast<int>(GetLastError()), std::system_category()};
}

struct OwnedImportFile {
    fs::path path;
    HANDLE handle = INVALID_HANDLE_VALUE;

    OwnedImportFile() = default;
    OwnedImportFile(fs::path ownedPath, HANDLE ownedHandle)
        : path(std::move(ownedPath)), handle(ownedHandle) {
    }
    OwnedImportFile(const OwnedImportFile&) = delete;
    OwnedImportFile& operator=(const OwnedImportFile&) = delete;
    OwnedImportFile(OwnedImportFile&& other) noexcept
        : path(std::move(other.path)), handle(other.handle) {
        other.handle = INVALID_HANDLE_VALUE;
    }
    OwnedImportFile& operator=(OwnedImportFile&& other) noexcept {
        if (this != &other) {
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
            }
            path = std::move(other.path);
            handle = other.handle;
            other.handle = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    ~OwnedImportFile() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

fs::path ResolveAvailableDestination(
    const fs::path& destinationDirectory,
    const fs::path& sourceFile
) {
    fs::path destination = destinationDirectory / sourceFile.filename();
    if (!fs::exists(destination)) {
        return destination;
    }

    const std::wstring stem = destination.stem().wstring();
    const std::wstring extension = destination.extension().wstring();
    for (unsigned int suffix = 1; ; ++suffix) {
        destination = destinationDirectory
            / (stem + L"_" + std::to_wstring(suffix) + extension);
        if (!fs::exists(destination)) {
            return destination;
        }
    }
}

OwnedImportFile CreateOwnedStagingFile(
    const fs::path& destinationDirectory,
    const fs::path& sourceFile,
    std::error_code& error
) {
    error.clear();
    const auto timestamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::wstring baseName = sourceFile.filename().wstring()
        + L".textmagic-import-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(timestamp);
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        const fs::path candidate = destinationDirectory
            / (baseName + L"-" + std::to_wstring(attempt) + L".tmp");
        HANDLE handle = CreateFileW(
            candidate.c_str(),
            GENERIC_READ | GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY,
            nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            return OwnedImportFile(candidate, handle);
        }
        const DWORD createError = GetLastError();
        if (createError != ERROR_FILE_EXISTS && createError != ERROR_ALREADY_EXISTS) {
            error = {static_cast<int>(createError), std::system_category()};
            return {};
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}

bool CopyIntoOwnedFile(
    const fs::path& sourcePath,
    HANDLE destination,
    std::error_code& error
) {
    error.clear();
    HANDLE source = CreateFileW(
        sourcePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (source == INVALID_HANDLE_VALUE) {
        error = LastWin32Error();
        return false;
    }

    LARGE_INTEGER beginning = {};
    if (!SetFilePointerEx(destination, beginning, nullptr, FILE_BEGIN)
        || !SetEndOfFile(destination)) {
        error = LastWin32Error();
        CloseHandle(source);
        return false;
    }

    std::array<unsigned char, 64 * 1024> buffer = {};
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(
                source,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesRead,
                nullptr)) {
            error = LastWin32Error();
            CloseHandle(source);
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        DWORD offset = 0;
        while (offset < bytesRead) {
            DWORD bytesWritten = 0;
            if (!WriteFile(
                    destination,
                    buffer.data() + offset,
                    bytesRead - offset,
                    &bytesWritten,
                    nullptr)
                || bytesWritten == 0) {
                error = LastWin32Error();
                CloseHandle(source);
                return false;
            }
            offset += bytesWritten;
        }
    }
    CloseHandle(source);
    return true;
}

void RecordRollbackFailure(
    const fs::path& path,
    std::vector<fs::path>* failedPaths
) noexcept {
    if (!failedPaths) {
        return;
    }
    try {
        failedPaths->push_back(path);
    } catch (...) {
    }
}

void RollBackFile(
    OwnedImportFile* file,
    const ArchiveRemoveOperation& requestedRemoveOperation,
    std::vector<fs::path>* failedPaths
) noexcept {
    if (!file || file->handle == INVALID_HANDLE_VALUE) {
        return;
    }

    bool removed = false;
    if (requestedRemoveOperation) {
        std::error_code removeError;
        try {
            removed = requestedRemoveOperation(file->path, removeError) && !removeError;
        } catch (...) {
        }
    } else {
        FILE_DISPOSITION_INFO disposition = {};
        disposition.DeleteFile = TRUE;
        removed = SetFileInformationByHandle(
            file->handle,
            FileDispositionInfo,
            &disposition,
            sizeof(disposition)) != FALSE;
    }
    if (!removed) {
        RecordRollbackFailure(file->path, failedPaths);
    }
    CloseHandle(file->handle);
    file->handle = INVALID_HANDLE_VALUE;
}

void RollBackImport(
    ArchiveImportResult* result,
    OwnedImportFile* currentFile,
    std::vector<OwnedImportFile>* importedFiles,
    const ArchiveRemoveOperation& removeOperation
) noexcept {
    if (!result) {
        return;
    }
    if (currentFile && currentFile->handle != INVALID_HANDLE_VALUE) {
        RollBackFile(currentFile, removeOperation, &result->rollbackFailedPaths);
    }
    if (importedFiles) {
        for (auto it = importedFiles->rbegin(); it != importedFiles->rend(); ++it) {
            RollBackFile(&*it, removeOperation, &result->rollbackFailedPaths);
        }
    }
    result->importedPaths.clear();
}

}

ScopedPathCleanup::ScopedPathCleanup(fs::path path)
    : m_path(std::move(path)) {
}

ScopedPathCleanup::~ScopedPathCleanup() {
    if (m_active) {
        std::error_code ignored;
        fs::remove_all(m_path, ignored);
    }
}

bool ScopedPathCleanup::Cleanup(std::error_code& error) noexcept {
    error.clear();
    if (!m_active) {
        return true;
    }
    fs::remove_all(m_path, error);
    if (!error) {
        m_active = false;
    }
    return !error;
}

void ScopedPathCleanup::Release() noexcept {
    m_active = false;
}

ArchiveImportResult ImportArchiveFilesTransactionally(
    const fs::path& destinationDirectory,
    const std::vector<fs::path>& sourceFiles,
    const ArchiveCopyOperation& requestedCopyOperation,
    const ArchiveRemoveOperation& requestedRemoveOperation
) {
    ArchiveImportResult result;
    std::error_code error;
    fs::create_directories(destinationDirectory, error);
    if (error) {
        result.error = error;
        return result;
    }

    std::vector<OwnedImportFile> importedFiles;
    OwnedImportFile currentFile;
    try {
        importedFiles.reserve(sourceFiles.size());
        result.importedPaths.reserve(sourceFiles.size());
        for (const fs::path& source : sourceFiles) {
            if (!fs::is_regular_file(source, error) || error) {
                result.error = error
                    ? error
                    : std::make_error_code(std::errc::invalid_argument);
                RollBackImport(
                    &result, &currentFile, &importedFiles, requestedRemoveOperation);
                return result;
            }

            const fs::path destination =
                ResolveAvailableDestination(destinationDirectory, source);
            currentFile = CreateOwnedStagingFile(destinationDirectory, source, error);
            if (currentFile.handle == INVALID_HANDLE_VALUE || error) {
                result.error = error
                    ? error
                    : std::make_error_code(std::errc::io_error);
                RollBackImport(
                    &result, &currentFile, &importedFiles, requestedRemoveOperation);
                return result;
            }
            error.clear();
            const bool copied = requestedCopyOperation
                ? requestedCopyOperation(source, currentFile.path, error)
                : CopyIntoOwnedFile(source, currentFile.handle, error);
            if (!copied || error) {
                result.error = error
                    ? error
                    : std::make_error_code(std::errc::io_error);
                RollBackImport(
                    &result, &currentFile, &importedFiles, requestedRemoveOperation);
                return result;
            }
            if (!FlushFileBuffers(currentFile.handle)
                || !MoveFileW(currentFile.path.c_str(), destination.c_str())) {
                result.error = LastWin32Error();
                RollBackImport(
                    &result, &currentFile, &importedFiles, requestedRemoveOperation);
                return result;
            }
            currentFile.path = destination;
            result.importedPaths.push_back(destination);
            importedFiles.push_back(std::move(currentFile));
        }
    } catch (...) {
        result.error = std::make_error_code(std::errc::io_error);
        RollBackImport(
            &result, &currentFile, &importedFiles, requestedRemoveOperation);
        return result;
    }

    for (OwnedImportFile& file : importedFiles) {
        if (file.handle != INVALID_HANDLE_VALUE) {
            CloseHandle(file.handle);
            file.handle = INVALID_HANDLE_VALUE;
        }
    }
    result.success = true;
    return result;
}

fs::path CreateUniqueTemporaryDirectory(
    const fs::path& parentDirectory,
    const std::wstring& prefix,
    std::error_code& error
) {
    error.clear();
    if (!fs::is_directory(parentDirectory, error) || error) {
        if (!error) {
            error = std::make_error_code(std::errc::not_a_directory);
        }
        return {};
    }

    const auto timestamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::wstring baseName = prefix
        + L"-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(timestamp);
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        const fs::path candidate = parentDirectory
            / (baseName + L"-" + std::to_wstring(attempt));
        if (fs::create_directory(candidate, error)) {
            return candidate;
        }
        if (error) {
            return {};
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}

fs::path CreateSiblingStagingArchivePath(
    const fs::path& targetArchive,
    std::error_code& error
) {
    error.clear();
    fs::path directory = targetArchive.parent_path();
    if (directory.empty()) {
        directory = fs::current_path(error);
        if (error) {
            return {};
        }
    }
    if (!fs::is_directory(directory, error) || error) {
        if (!error) {
            error = std::make_error_code(std::errc::not_a_directory);
        }
        return {};
    }

    const auto timestamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::wstring baseName = targetArchive.filename().wstring()
        + L".textmagic-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(timestamp);
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        const fs::path candidate = directory
            / (baseName + L"-" + std::to_wstring(attempt) + L".zip");
        if (!fs::exists(candidate, error) && !error) {
            return candidate;
        }
        if (error) {
            return {};
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}

bool CommitStagedArchive(
    const fs::path& stagedArchive,
    const fs::path& targetArchive,
    std::error_code& error
) {
    error.clear();
    if (!fs::is_regular_file(stagedArchive, error) || error) {
        if (!error) {
            error = std::make_error_code(std::errc::no_such_file_or_directory);
        }
        return false;
    }

    const std::wstring staged = stagedArchive.wstring();
    const std::wstring target = targetArchive.wstring();
    if (fs::exists(targetArchive, error)) {
        if (error) {
            return false;
        }
        if (ReplaceFileW(
                target.c_str(), staged.c_str(), nullptr,
                REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
            return true;
        }
        error = LastWin32Error();
        return false;
    }
    if (error) {
        return false;
    }

    if (MoveFileExW(
            staged.c_str(), target.c_str(),
            MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
    error = LastWin32Error();
    return false;
}

bool ExportArchiveTransactionally(
    const fs::path& targetArchive,
    const ArchiveProducer& producer,
    std::error_code& error,
    ArchiveExportFailure* failure
) {
    error.clear();
    if (failure) {
        *failure = ArchiveExportFailure::None;
    }

    const fs::path stagingArchive =
        CreateSiblingStagingArchivePath(targetArchive, error);
    if (stagingArchive.empty()) {
        if (failure) {
            *failure = ArchiveExportFailure::PrepareStaging;
        }
        return false;
    }

    ScopedPathCleanup stagingCleanup(stagingArchive);
    bool produced = false;
    try {
        produced = producer && producer(stagingArchive);
    } catch (...) {
        error = std::make_error_code(std::errc::io_error);
    }
    if (!produced) {
        if (failure) {
            *failure = ArchiveExportFailure::Produce;
        }
        const std::error_code producerError = error
            ? error
            : std::make_error_code(std::errc::io_error);
        std::error_code cleanupError;
        if (!stagingCleanup.Cleanup(cleanupError)) {
            error = cleanupError;
        } else {
            error = producerError;
        }
        return false;
    }

    if (!CommitStagedArchive(stagingArchive, targetArchive, error)) {
        if (failure) {
            *failure = ArchiveExportFailure::Commit;
        }
        std::error_code cleanupError;
        if (!stagingCleanup.Cleanup(cleanupError) && !error) {
            error = cleanupError;
        }
        return false;
    }

    stagingCleanup.Release();
    return true;
}
