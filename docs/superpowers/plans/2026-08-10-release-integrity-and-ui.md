# Release Integrity and UI Implementation Plan

**Goal:** Ship stable `1.0.0` with checksum-verified updates, dark native title bars, and copyable message dialogs.

**Constraints:** Reuse the existing updater and clipboard helper, add no third-party dependency, and preserve every script body and hotkey while updating descriptions.

### Task 1: Checksum tests and implementation

- Extend `tests/UpdateServiceTests.cpp` with failing checksum generation and verification checks.
- Add the smallest native SHA-256 helper to `UpdateService`.
- Generate `SHA256SUMS.txt` beside the running executable without blocking startup on write failure.
- Download and validate `SHA256SUMS.txt` before accepting a release executable.

### Task 2: UI changes

- Add one reusable native dark-title-bar helper and call it for all captioned application windows.
- Reuse the existing second dialog button as a non-closing localized copy action for one-button messages.

### Task 3: Verification and publication

- Give every supplied `.tmscript` a concise English `description`.
- Reconfigure and rebuild the root `build` directory from scratch.
- Run all CTest tests and `git diff --check`.
- Launch `TextMagic.exe`, validate its generated checksum file, and close it.
- Commit intended tracked changes, push `master`, create and push `v1.0.0`.
- Create the normal GitHub Release `1.0.0` with `TextMagic.exe` and `SHA256SUMS.txt`.
