# Release Integrity and UI Design

## Goal

Publish stable TextMagic `0.1.0` as the normal GitHub latest release and make the existing updater verify the downloaded executable with the release asset `SHA256SUMS.txt`.

## Release and update contract

- Application version: `0.1.0`.
- Git tag: `v0.1.0`.
- Release asset names: `TextMagic.exe` and `SHA256SUMS.txt`.
- `SHA256SUMS.txt` contains one conventional SHA-256 entry for `TextMagic.exe`.
- On every successful primary-instance startup, TextMagic rewrites `SHA256SUMS.txt` beside its executable. A write failure does not block startup and is recorded in the log.
- The updater keeps using GitHub's `/releases/latest` redirect, downloads both assets from the resolved tag, and replaces the application only after the downloaded executable matches the published checksum.
- Version comparison remains numeric for the stable `major.minor.patch` version.

## UI changes

- Apply the native immersive-dark title-bar attribute to the main, information, and message windows, with the older Windows attribute as fallback.
- Every one-button styled message receives a localized `Copy` button that copies the complete message and leaves the dialog open. Confirmation dialogs retain their existing primary and secondary actions.

## Verification

- Add focused executable tests for checksum creation, parsing, matching, and mismatch rejection.
- Run the complete Release build and CTest suite.
- Start the rebuilt executable once, verify the generated `SHA256SUMS.txt` against `TextMagic.exe`, then publish both files in GitHub Release `0.1.0`.
