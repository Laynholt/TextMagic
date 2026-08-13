#include "ArchiveImportPolicy.h"
#include "PowerShellUtils.h"
#include "ScriptRunner.h"

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

bool CreateArchive(
    const ScriptRunner& runner,
    const fs::path& sourceDirectory,
    const fs::path& archive,
    std::wstring* error
) {
    const std::wstring source =
        PowerShellUtils::EscapeSingleQuoted(sourceDirectory.wstring());
    const std::wstring destination =
        PowerShellUtils::EscapeSingleQuoted(archive.wstring());
    const std::wstring script =
        L"$ErrorActionPreference='Stop'\n"
        L"$files=Get-ChildItem -LiteralPath '" + source + L"' -File\n"
        L"Compress-Archive -LiteralPath $files.FullName -DestinationPath '"
        + destination + L"' -Force\n";
    std::wstring output;
    return runner.ExecutePowerShellScript(script, L"", &output, error);
}

bool RunImport(
    const ScriptRunner& runner,
    const fs::path& archive,
    const fs::path& destination,
    const ArchiveImportLimits& limits,
    const ArchiveImportMessages& messages,
    std::wstring* error
) {
    std::error_code cleanupError;
    fs::remove_all(destination, cleanupError);
    fs::create_directories(destination);
    const std::wstring script = BuildArchivePreflightAndExtractScript(
        archive.wstring(), destination.wstring(), limits, messages);
    std::wstring output;
    return runner.ExecutePowerShellScript(script, L"", &output, error);
}
}

int main() {
    const fs::path root = fs::temp_directory_path()
        / (L"TextMagic-archive-policy-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    fs::create_directories(root / L"source");
    WriteFile(root / L"source" / L"first.tmscript", "1234");
    WriteFile(root / L"source" / L"second.tmscript", "5678");

    const ScriptRunner runner;
    const fs::path archive = root / L"scripts.zip";
    std::wstring error;
    Expect(CreateArchive(runner, root / L"source", archive, &error),
           "the ZIP policy fixture archive must be created");

    const ArchiveImportMessages messages{
        L"entry limit exceeded",
        L"file limit exceeded",
        L"total limit exceeded",
    };
    const fs::path destination = root / L"extracted";

    Expect(RunImport(
               runner, archive, destination, {2, 4, 8}, messages, &error),
           "an archive exactly at every configured limit must extract");
    Expect(fs::is_regular_file(destination / L"first.tmscript")
               && fs::is_regular_file(destination / L"second.tmscript"),
           "a valid boundary archive must expose all extracted files");

    Expect(!RunImport(
               runner, archive, destination, {1, 4, 8}, messages, &error)
               && error.find(messages.entryLimitExceeded) != std::wstring::npos,
           "an archive with too many entries must fail before extraction");
    Expect(fs::is_empty(destination),
           "an entry-limit failure must leave the extraction directory empty");

    Expect(!RunImport(
               runner, archive, destination, {2, 3, 8}, messages, &error)
               && error.find(messages.fileLimitExceeded) != std::wstring::npos,
           "an archive with an oversized file must fail before extraction");
    Expect(fs::is_empty(destination),
           "a file-limit failure must leave the extraction directory empty");

    Expect(!RunImport(
               runner, archive, destination, {2, 4, 7}, messages, &error)
               && error.find(messages.totalLimitExceeded) != std::wstring::npos,
           "an archive over the total expanded-size limit must fail before extraction");
    Expect(fs::is_empty(destination),
           "a total-limit failure must leave the extraction directory empty");

    const ArchiveImportLimits defaults = DefaultArchiveImportLimits();
    Expect(defaults.maxEntries == 1024
               && defaults.maxFileBytes == 64ull * 1024ull * 1024ull
               && defaults.maxTotalBytes == 256ull * 1024ull * 1024ull,
           "production ZIP import limits must match the approved resource boundary");

    fs::remove_all(root, cleanupError);
    return 0;
}
