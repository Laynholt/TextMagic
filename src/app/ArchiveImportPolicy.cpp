#include "ArchiveImportPolicy.h"

#include "PowerShellUtils.h"

namespace {
constexpr std::uint64_t kMiB = 1024ull * 1024ull;
}

ArchiveImportLimits DefaultArchiveImportLimits() noexcept {
    return {1024, 64 * kMiB, 256 * kMiB};
}

std::wstring BuildArchivePreflightAndExtractScript(
    const std::wstring& archivePath,
    const std::wstring& destinationPath,
    const ArchiveImportLimits& limits,
    const ArchiveImportMessages& messages
) {
    const std::wstring escapedArchive =
        PowerShellUtils::EscapeSingleQuoted(archivePath);
    const std::wstring escapedDestination =
        PowerShellUtils::EscapeSingleQuoted(destinationPath);
    const std::wstring escapedEntryMessage =
        PowerShellUtils::EscapeSingleQuoted(messages.entryLimitExceeded);
    const std::wstring escapedFileMessage =
        PowerShellUtils::EscapeSingleQuoted(messages.fileLimitExceeded);
    const std::wstring escapedTotalMessage =
        PowerShellUtils::EscapeSingleQuoted(messages.totalLimitExceeded);

    return
        L"$ErrorActionPreference='Stop'\n"
        L"Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
        L"$archivePath='" + escapedArchive + L"'\n"
        L"$destinationPath='" + escapedDestination + L"'\n"
        L"[uint64]$maxEntries=" + std::to_wstring(limits.maxEntries) + L"\n"
        L"[uint64]$maxFileBytes=" + std::to_wstring(limits.maxFileBytes) + L"\n"
        L"[uint64]$maxTotalBytes=" + std::to_wstring(limits.maxTotalBytes) + L"\n"
        L"$zip=$null\n"
        L"try {\n"
        L"  $zip=[System.IO.Compression.ZipFile]::OpenRead($archivePath)\n"
        L"  if ([uint64]$zip.Entries.Count -gt $maxEntries) { throw '"
            + escapedEntryMessage + L"' }\n"
        L"  [uint64]$totalBytes=0\n"
        L"  foreach ($entry in $zip.Entries) {\n"
        L"    if ([string]::IsNullOrEmpty($entry.Name)) { continue }\n"
        L"    [uint64]$entryBytes=$entry.Length\n"
        L"    if ($entryBytes -gt $maxFileBytes) { throw '"
            + escapedFileMessage + L"' }\n"
        L"    if ($totalBytes -gt $maxTotalBytes -or $entryBytes -gt ($maxTotalBytes - $totalBytes)) { throw '"
            + escapedTotalMessage + L"' }\n"
        L"    $totalBytes += $entryBytes\n"
        L"  }\n"
        L"} finally {\n"
        L"  if ($null -ne $zip) { $zip.Dispose() }\n"
        L"}\n"
        L"Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath -Force\n";
}
