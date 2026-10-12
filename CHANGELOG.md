# Changelog

The changes of each release of Silent Heritage, newest first. A release's
notes on GitHub are its section here: the release workflow refuses a tag
whose version has no section (RELEASING.md). The versions follow
[Semantic Versioning](https://semver.org); a version with a suffix
(`1.0.0-beta.1`) is a pre-release.

## [Unreleased]

## [0.1.0] - 2026-10-12

The first release: the whole game, in the variant faithful to the original
(FAITHFUL.md) and in the extended one (EXTENDED.md).

### Added

- Release packages, built and published by the repository's workflows:
  Android (one package for arm64-v8a and x86_64), Linux (tarball and
  AppImage, x86_64 and aarch64) and Windows (zip, x64), each for the
  faithful and the extended variant.
- Android: the saves are kept by Android's backup.
- Windows: the program's icon and version information; no console window.
