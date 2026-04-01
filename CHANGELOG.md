# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows Semantic Versioning.

## [Unreleased]

### Changed

- Repository reorganized into `apps/desktop`, `apps/bootstrap`, and `docs/project`
- Public repository metadata, GitHub Actions workflows, and GitHub Pages site added
- Search results improved with full-range selection and byte previews
- Busy-state feedback added for search and hash operations

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

[Unreleased]: https://github.com/majimboo/hex-master/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/majimboo/hex-master/releases/tag/v1.0.0
