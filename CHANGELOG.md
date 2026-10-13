# Changelog

## [Unreleased]

### Added

- macOS packages: a disk image per version, with one app for Apple Silicon
  and Intel Macs, macOS 13.3 or later. Not notarized: the first start needs
  "Open Anyway" in System Settings.

### Fixed

- The ARM builds (Linux aarch64, Android on ARM) compute the sun's position,
  and so the daylight colours, exactly as the others.

## [0.1.0] - 2026-10-12

The first release: the whole game, in the classic version
([CLASSIC.md](CLASSIC.md)) and the extended one ([EXTENDED.md](EXTENDED.md)).

### Added

- Release packages for Android (arm64-v8a and x86_64), Linux (tarball and
  AppImage, x86_64 and aarch64) and Windows (zip, x64), for both versions.
- Android: the saves are kept by Android's backup.
- Windows: the program's icon and version information; no console window.
