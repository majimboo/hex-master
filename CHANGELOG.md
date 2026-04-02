# Changelog

## [1.2.1] - 2026-04-03

### Added

- Windows Explorer context-menu integration with install and remove actions from the Tools menu

### Changed

- Explorer `Edit with Hex Master` launches now open the selected file immediately instead of falling through to session restore

## [1.2.0] - 2026-04-02

### Added

- 3D Buffer Explorer tool window for 3D model reverse engineering, candidate scanning, and live geometry preview
- Extended schema DSL with staged `repeat alias in source { ... }` blocks and dotted references inside repeat scopes
- Schema JSON export from the Schema Editor
- `Insert Bytes...` workflow, including append-at-end support and context-menu insertion after a clicked byte
- Example `item_obj.hms` schema for staged mesh-style file layouts

### Changed

- Structural save behavior now uses faster in-place paths when safe and streams general structural saves instead of materializing the whole rewritten file in memory first
- The 3D explorer now supports both `XYZ together` and `X block, Y block, Z block` layouts for raw geometry exploration
- Public docs, schema guide, README, and Pages site were updated to reflect the current schema, save, and reverse-engineering toolset

## [1.1.0] - 2026-04-02

### Added

- Schema Editor tool window for creating, opening, saving, validating, and running `.hms` schema files
- Custom binary structure DSL with named structs, root blocks, arrays, offset-based fields, and expression-based counts and offsets
- Schema-specific recent files and in-app syntax guide access
- Coverage reporting after schema runs, including percent of the remaining file covered from the chosen base offset
- Public schema documentation in the project docs and GitHub Pages site

### Changed

- Structure results are now populated lazily with chunked large-array expansion, avoiding long freezes after schema runs on large files
- Schema runs now show progress and support cancel during evaluation
- Documentation and README updated to describe schema support and expression-based dependent arrays

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

[1.2.1]: https://github.com/majimboo/hex-master/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/majimboo/hex-master/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/majimboo/hex-master/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/majimboo/hex-master/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/majimboo/hex-master/releases/tag/v1.0.0
