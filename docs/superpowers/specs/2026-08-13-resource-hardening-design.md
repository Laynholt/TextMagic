# Resource Hardening Design

## Goal

Close three bounded-resource and recovery gaps without changing TextMagic's supported UI or script behavior:

- bound the total memory duplicated by a clipboard snapshot;
- reject ZIP imports that exceed fixed entry, per-file, or total expanded-size limits before extraction;
- report incomplete ZIP import rollback instead of silently ignoring failed removals.

## Clipboard Snapshot Boundary

The clipboard snapshot may duplicate at most 64 MiB across all enumerated formats. Before calling `OleDuplicateData`, TextMagic obtains the source format size when the format uses global memory and adds it to a checked running total. If the next format would exceed the limit, or its size cannot be represented safely, the snapshot becomes incomplete and stops duplicating additional formats.

An incomplete snapshot remains non-restorable. Existing duplicated handles stay owned by the snapshot and are released by its destructor. Ordinary clipboard contents below the limit retain the existing full-format preservation behavior.

Bitmap, palette, and metafile handles do not expose a reliable byte size through `GlobalSize`. They continue through the existing duplication path and count as zero toward the byte limit; the limit therefore bounds ordinary memory-backed formats rather than guaranteeing an exact process-wide allocation ceiling for opaque GDI objects.

## ZIP Import Boundary

Before `Expand-Archive`, TextMagic runs a PowerShell preflight using `System.IO.Compression.ZipArchive` and rejects an archive when any condition is true:

- more than 1024 entries;
- any file entry has an uncompressed length above 64 MiB;
- the checked sum of uncompressed file lengths exceeds 256 MiB.

Directory entries do not count toward the per-file size but do count toward the 1024-entry ceiling. The summation checks overflow before addition. The preflight and extraction execute in one background operation, and extraction starts only after preflight succeeds. Existing valid ZIP archives within all limits import as before.

These limits bound declared uncompressed sizes. They do not attempt to replace Windows ZIP parsing or validate every compressed-data inconsistency; extraction errors still fail before the scripts directory is modified.

## Transactional Rollback

`ArchiveImportResult` records rollback removal failures in `rollbackFailedPaths`. When copying the current file or a later file fails, rollback attempts every created destination in reverse order and records each path that still exists because removal failed.

If rollback is complete, the import reports the original copy error. If rollback is incomplete, the result remains unsuccessful, preserves the original error, and also returns the undeleted paths. The application reloads the scripts list so the UI matches disk state, logs each undeleted path, and displays an explicit incomplete-rollback error. No already-existing destination file is ever selected for rollback.

## Testing

Focused regression tests will cover:

- clipboard accounting accepts values at the 64 MiB boundary and rejects the next byte without overflow;
- ZIP policy accepts boundary values and rejects excessive entries, a file over 64 MiB, a total over 256 MiB, and arithmetic overflow;
- rollback reports a simulated removal failure while continuing attempts for the remaining created files;
- existing successful snapshot, ZIP import transaction, and export transaction behavior remains intact.

The final verification gate is a fresh Release configuration and build, all CTest tests, `git diff --check`, and a full Codebase Memory reindex verified by querying the new policy and rollback symbols.

## Compatibility and Scope

- C++17 and Windows 10 compatibility remain unchanged.
- No new third-party dependency or user-facing setting is introduced.
- Custom UI widgets and rendering are unchanged.
- Export ZIP behavior and limits are unchanged because exported content originates from the trusted local scripts directory and already uses transactional staging.
