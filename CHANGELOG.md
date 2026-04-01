# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows Semantic Versioning.

## [Unreleased]

- No released changes yet.

## [1.0.1] - 2026-04-02

### Added

- Search and replace history with remembered mode, action, and selection options
- Tabbed search and replace result sets that keep previous runs available
- Search progress for `Find Next`, `Find Previous`, and `Find All`
- Replace progress for `Replace Next` and `Replace All`
- Technical README notes describing the large-file handling approach

### Changed

- Search progress reporting now advances smoothly on large files
- Replace operations now show result tables with replaced offsets, including partial results after canceling `Replace All`
- Large-file save and backup progress reporting was refined for better responsiveness on multi-GB writes

## [1.0.0] - 2026-04-01

### Added

- Windows desktop hex editor shell built with Qt and a Rust backend
- Hex and text-side editing
- Insert and overwrite modes
- Structural insert, delete, cut, and paste behavior
- Undo and redo support through the Rust bridge
- Read-only awareness and Save As fallback
- Recent files, session restore, and persistent layout/settings
- Bookmarks dock and bookmark navigation
- Inspector dock with grouped typed interpretations
- Analysis dock with CRC32, MD5, SHA-1, and SHA-256
- Multi-mode search:
  - text search
  - raw hex-byte search
  - typed-value search
- Unified replace workflow with replace-next and replace-all
- Search results table with match navigation
- About dialog and application version metadata
- Embedded application icon for Qt runtime and Windows executable metadata

### Changed

- Search results now highlight the full matched range
- Search and hash operations now show visible busy state feedback
- Public release output standardized around `HexMaster.exe`

### Infrastructure

- Versioning unified under `apps/desktop/CMakeLists.txt`
- Windows version resource template added
- GitHub CI, release automation, and Pages deployment added
- README, build docs, release docs, and license added for public release readiness

[Unreleased]: https://github.com/majimboo/hex-master/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/majimboo/hex-master/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/majimboo/hex-master/releases/tag/v1.0.0
