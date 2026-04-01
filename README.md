<p align="center">
  <img src="appicon.png" alt="Hex Master logo" width="160">
</p>

# Hex Master

Hex Master is a Windows-first hex editor and binary file editor built with a Rust core and a Qt desktop shell.

It is designed as a modern desktop alternative to Hex Workshop for inspecting and editing binary data, executable files, save files, firmware images, and other raw byte-oriented formats.

![Hex Master main window](docs/assets/screenshots/hexmaster-main-window.png)

## Features

- hex-side and text-side editing
- insert and overwrite modes
- structural insert, delete, cut, and paste behavior
- typed inspector views for integer, floating-point, and time interpretations
- byte-pattern, text, and typed-value search
- unified replace workflow for replace-next and replace-all
- search results table with match navigation
- bookmarks, checksums, recent files, and session restore

## Download

Project site:

- https://majimboo.github.io/hex-master/

Releases:

- https://github.com/majimboo/hex-master/releases

When a tagged release is published, the Windows package will be available as:

- `HexMaster-windows-x64-vX.Y.Z.zip`

After extracting that archive, run:

- `HexMaster.exe`

## Platform Support

- Windows: primary supported release target
- Linux: source build may be possible with Qt 6 and a native toolchain, but packaged releases are not set up yet
- macOS: source build may be possible later, but packaged releases are not set up yet

## Build From Source

Prerequisites:

- Rust toolchain
- CMake 3.27+
- Qt 6 for MSVC on Windows
- Visual Studio 2022 build tools or full IDE

Debug build:

```powershell
.\scripts\build.ps1
```

Release build:

```powershell
.\scripts\build.ps1 -Configuration Release
```

Rust-only build:

```powershell
.\scripts\build.ps1 -SkipQt
```

More build details:

- [docs/project/BUILD.md](docs/project/BUILD.md)

## Versioning

Hex Master follows semantic versioning.

- `MAJOR`: breaking changes
- `MINOR`: backward-compatible features
- `PATCH`: backward-compatible fixes and polish

Version is defined in one place:

- [apps/desktop/CMakeLists.txt](apps/desktop/CMakeLists.txt)

That version feeds:

- application metadata
- the About dialog
- Windows executable version information

## Release Process

Recommended release flow:

1. bump the version in `apps/desktop/CMakeLists.txt`
2. commit the version change
3. create and push a matching tag such as `v1.0.1`
4. GitHub Actions builds and publishes the release archive

Release automation:

- [.github/workflows/release.yml](.github/workflows/release.yml)
- [docs/project/RELEASE.md](docs/project/RELEASE.md)

## Repository Layout

- `apps/desktop`: main Qt desktop application
- `apps/bootstrap`: Rust-side bootstrap crate
- `crates/hexapp-core`: core document and selection logic
- `crates/hexapp-io`: file-backed I/O support
- `crates/hexapp-search`: search-related logic
- `crates/hexapp-analysis`: hashes and analysis helpers
- `crates/hexapp-inspector`: typed data interpretation
- `crates/hexapp-session`: settings and session persistence
- `crates/hexapp-ffi`: Rust bridge consumed by the desktop shell
- `docs/`: GitHub Pages site and public-facing project docs
- `docs/project/`: engineering, roadmap, build, and release documents
- `scripts/`: local bootstrap and build scripts

## Automation

This repository includes:

- CI workflow for Rust tests and Windows Qt build
- release workflow for tagged Windows artifacts
- Pages deployment workflow for the public landing page

Workflow files:

- [.github/workflows/ci.yml](.github/workflows/ci.yml)
- [.github/workflows/release.yml](.github/workflows/release.yml)
- [.github/workflows/pages.yml](.github/workflows/pages.yml)

## Documentation

- [CHANGELOG.md](CHANGELOG.md)
- [docs/project/PRODUCT_SPEC.md](docs/project/PRODUCT_SPEC.md)
- [docs/project/ARCHITECTURE.md](docs/project/ARCHITECTURE.md)
- [docs/project/ROADMAP.md](docs/project/ROADMAP.md)
- [docs/project/MVP_CHECKLIST.md](docs/project/MVP_CHECKLIST.md)
- [docs/project/BUILD.md](docs/project/BUILD.md)
- [docs/project/RELEASE.md](docs/project/RELEASE.md)

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).

## Author

Created by Majid Siddiqui  
me@majidarif.com  
2026
