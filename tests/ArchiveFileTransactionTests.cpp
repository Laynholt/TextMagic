#include "ArchiveFileTransaction.h"

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void WriteFile(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}
}

int main() {
    const fs::path root = fs::temp_directory_path()
        / (L"TextMagic-archive-transaction-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    fs::create_directories(root / L"sources");
    fs::create_directories(root / L"scripts");

    const fs::path firstSource = root / L"sources" / L"first.tmscript";
    const fs::path secondSource = root / L"sources" / L"second.tmscript";
    const fs::path thirdSource = root / L"sources" / L"third.tmscript";
    WriteFile(firstSource, "first");
    WriteFile(secondSource, "second");
    WriteFile(thirdSource, "third");

    int copyAttempt = 0;
    const ArchiveImportResult failedImport = ImportArchiveFilesTransactionally(
        root / L"scripts",
        {firstSource, secondSource},
        [&](const fs::path& source, const fs::path& destination, std::error_code& error) {
            error.clear();
            ++copyAttempt;
            if (copyAttempt == 2) {
                error = std::make_error_code(std::errc::permission_denied);
                return false;
            }
            WriteFile(destination, ReadFile(source));
            return true;
        }
    );
    Expect(!failedImport.success, "a failed archive import transaction must fail");
    Expect(failedImport.importedPaths.empty(),
           "a failed archive import transaction must not report imported files");
    Expect(!fs::exists(root / L"scripts" / L"first.tmscript"),
           "a failed archive import transaction must roll back earlier copies");
    Expect(!fs::exists(root / L"scripts" / L"second.tmscript"),
           "a failed archive import transaction must not leave the failed copy");

    copyAttempt = 0;
    const ArchiveImportResult throwingImport = ImportArchiveFilesTransactionally(
        root / L"scripts",
        {firstSource, secondSource},
        [&](const fs::path& source, const fs::path& destination, std::error_code& error) {
            error.clear();
            ++copyAttempt;
            if (copyAttempt == 2) {
                WriteFile(destination, "partial");
                throw std::runtime_error("copy failed unexpectedly");
            }
            WriteFile(destination, ReadFile(source));
            return true;
        }
    );
    Expect(!throwingImport.success,
           "an exceptional archive import transaction must fail without escaping");
    Expect(throwingImport.importedPaths.empty(),
           "an exceptional archive import transaction must clear imported paths");
    Expect(!fs::exists(root / L"scripts" / L"first.tmscript"),
           "an exceptional archive import transaction must roll back earlier copies");
    Expect(!fs::exists(root / L"scripts" / L"second.tmscript"),
           "an exceptional archive import transaction must remove its partial copy");

    const fs::path racedDestination = root / L"scripts" / L"first.tmscript";
    const ArchiveImportResult racedImport = ImportArchiveFilesTransactionally(
        root / L"scripts",
        {firstSource},
        [&](const fs::path& source, const fs::path& stagingPath, std::error_code& error) {
            error.clear();
            WriteFile(stagingPath, ReadFile(source));
            WriteFile(racedDestination, "foreign");
            return true;
        });
    Expect(!racedImport.success,
           "a destination race must fail the archive import transaction");
    Expect(fs::exists(racedDestination) && ReadFile(racedDestination) == "foreign",
           "a destination race must never remove or overwrite the foreign file");
    fs::remove(racedDestination);

    copyAttempt = 0;
    int removeAttempt = 0;
    const ArchiveImportResult incompleteRollback = ImportArchiveFilesTransactionally(
        root / L"scripts",
        {firstSource, secondSource, thirdSource},
        [&](const fs::path& source, const fs::path& destination, std::error_code& error) {
            ++copyAttempt;
            if (copyAttempt == 3) {
                WriteFile(destination, "partial");
                error = std::make_error_code(std::errc::permission_denied);
                return false;
            }
            WriteFile(destination, ReadFile(source));
            return true;
        },
        [&](const fs::path& path, std::error_code& error) {
            ++removeAttempt;
            if (path.filename().wstring().find(L"third.tmscript.textmagic-import-") == 0) {
                error = std::make_error_code(std::errc::permission_denied);
                return false;
            }
            return fs::remove(path, error);
        });
    Expect(!incompleteRollback.success,
           "an incomplete archive rollback must keep the import failed");
    Expect(incompleteRollback.importedPaths.empty(),
           "an incomplete archive rollback must not report imported files");
    Expect(removeAttempt == 3,
           "archive rollback must continue after a removal failure");
    Expect(!fs::exists(root / L"scripts" / L"first.tmscript"),
           "archive rollback must remove other created files after a failure");
    Expect(!fs::exists(root / L"scripts" / L"second.tmscript"),
           "archive rollback must continue through all earlier created files");
    Expect(!fs::exists(root / L"scripts" / L"third.tmscript"),
           "a partial staging file must never be published as the final script");
    Expect(incompleteRollback.rollbackFailedPaths.size() == 1
               && fs::exists(incompleteRollback.rollbackFailedPaths.front())
               && incompleteRollback.rollbackFailedPaths.front().filename().wstring().find(
                   L"third.tmscript.textmagic-import-") == 0,
           "archive rollback must report the exact path it could not remove");
    fs::remove(incompleteRollback.rollbackFailedPaths.front());

    const fs::path targetArchive = root / L"scripts.zip";
    WriteFile(targetArchive, "original archive");
    fs::path failedStagingPath;
    std::error_code exportError;
    ArchiveExportFailure exportFailure = ArchiveExportFailure::None;
    Expect(!ExportArchiveTransactionally(
               targetArchive,
               [&](const fs::path& stagingArchive) {
                   failedStagingPath = stagingArchive;
                   WriteFile(stagingArchive, "partial archive");
                   return false;
               },
               exportError,
               &exportFailure),
           "a failed archive producer must fail the export transaction");
    Expect(exportFailure == ArchiveExportFailure::Produce,
           "a failed archive producer must identify the produce stage");
    Expect(static_cast<bool>(exportError),
           "a failed archive producer must return an actionable error");
    Expect(ReadFile(targetArchive) == "original archive",
           "a failed archive producer must preserve the existing archive");
    Expect(!failedStagingPath.empty() && !fs::exists(failedStagingPath),
           "a failed archive producer must remove its partial staging archive");

    fs::path exceptionalStagingPath;
    bool exportExceptionEscaped = false;
    try {
        Expect(!ExportArchiveTransactionally(
                   targetArchive,
                   [&](const fs::path& stagingArchive) -> bool {
                       exceptionalStagingPath = stagingArchive;
                       WriteFile(stagingArchive, "partial exceptional archive");
                       throw std::runtime_error("producer crashed");
                   },
                   exportError),
               "an exceptional archive producer must fail the export transaction");
    } catch (...) {
        exportExceptionEscaped = true;
    }
    Expect(!exportExceptionEscaped,
           "an archive producer exception must not escape the transaction");
    Expect(ReadFile(targetArchive) == "original archive",
           "an archive producer exception must preserve the existing archive");
    Expect(!exceptionalStagingPath.empty() && !fs::exists(exceptionalStagingPath),
           "an archive producer exception must remove its partial staging archive");

    const fs::path directoryTarget = root / L"directory-target.zip";
    fs::create_directory(directoryTarget);
    fs::path uncommittedStagingPath;
    Expect(!ExportArchiveTransactionally(
               directoryTarget,
               [&](const fs::path& stagingArchive) {
                   uncommittedStagingPath = stagingArchive;
                   WriteFile(stagingArchive, "valid but uncommittable archive");
                   return true;
               },
               exportError),
           "a commit failure must fail the export transaction");
    Expect(fs::is_directory(directoryTarget),
           "a failed archive commit must preserve the target");
    Expect(!uncommittedStagingPath.empty() && !fs::exists(uncommittedStagingPath),
           "a failed archive commit must remove its staging archive");

    std::error_code commitError;
    Expect(!CommitStagedArchive(root / L"missing.zip", targetArchive, commitError),
           "a missing staged archive must not commit");
    Expect(ReadFile(targetArchive) == "original archive",
           "a failed staged archive commit must preserve the existing archive");

    const fs::path stagedArchive = root / L"staged.zip";
    WriteFile(stagedArchive, "new archive");
    commitError.clear();
    Expect(CommitStagedArchive(stagedArchive, targetArchive, commitError),
           "a valid staged archive must replace the target");
    Expect(ReadFile(targetArchive) == "new archive",
           "a successful staged archive commit must expose the staged contents");
    Expect(!fs::exists(stagedArchive),
           "a successful staged archive commit must consume the staged file");

    const fs::path scopedDirectory = root / L"scoped";
    {
        fs::create_directories(scopedDirectory);
        WriteFile(scopedDirectory / L"temporary.txt", "temporary");
        ScopedPathCleanup cleanup(scopedDirectory);
    }
    Expect(!fs::exists(scopedDirectory),
           "scoped path cleanup must remove temporary directories recursively");

    std::error_code temporaryDirectoryError;
    const fs::path firstTemporaryDirectory = CreateUniqueTemporaryDirectory(
        root, L"extract", temporaryDirectoryError);
    Expect(!temporaryDirectoryError && fs::is_directory(firstTemporaryDirectory),
           "a unique temporary directory must be created atomically");
    const fs::path secondTemporaryDirectory = CreateUniqueTemporaryDirectory(
        root, L"extract", temporaryDirectoryError);
    Expect(!temporaryDirectoryError && fs::is_directory(secondTemporaryDirectory),
           "a second unique temporary directory must be created atomically");
    Expect(firstTemporaryDirectory != secondTemporaryDirectory,
           "temporary directory paths must be unique");

    fs::remove_all(root, cleanupError);
    return 0;
}
