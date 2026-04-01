# Hex Master

Hex Master is a Windows-first hex editor and binary file editor built with a Rust core and a Qt desktop shell.

It is intended as a modern desktop alternative to Hex Workshop for users who want a native hex editor for inspecting and editing binary data, executable files, save files, firmware images, and other raw byte-oriented formats.

![Hex Master main window](docs/assets/screenshots/hexmaster-main-window.png)

The current app is focused on practical desktop workflows:

- structural editing with insert and overwrite modes
- hex-side and text-side editing
- typed inspector views for integer, floating-point, and time interpretations
- byte-pattern, text, and typed-value search
- replace next and replace all from a unified replace workflow
- bookmarks, checksums, recent files, and session restore

Search-friendly keywords for this project:

- hex editor
- binary editor
- hex workshop alternative
- Windows hex editor
- Rust hex editor
- Qt hex editor

## Status

This repository now contains a functional desktop application rather than only a scaffold. The current release line starts at `1.0.0`, and versioning follows semantic versioning.

## Downloads

Once GitHub Releases is enabled for the repository, the latest downloadable build will be available from:

- `https://github.com/<owner>/<repo>/releases/latest`

The GitHub Pages landing site is designed to point at that latest release automatically.

Release asset naming:

- Windows executable inside the package: `HexMaster.exe`
- Windows release archive: `HexMaster-windows-x64-vX.Y.Z.zip`

## Build

Prerequisites:

- Rust toolchain
- CMake 3.27+
- Qt 6 for MSVC on Windows
- Visual Studio 2022 build tools or full IDE

### Platform Support

- Windows: primary supported release target
- Linux: source build may be possible with Qt 6 and a native toolchain, but release automation is not set up yet
- macOS: source build may be possible later, but release automation is not set up yet

The current public release pipeline is intentionally Windows-first until packaging and runtime validation are in place on other platforms.

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

## Versioning

Version is defined in one place:

- [apps/desktop/CMakeLists.txt](apps/desktop/CMakeLists.txt)

That version feeds:

- Qt application metadata
- the About dialog
- Windows executable version information

Version bump rules:

- `MAJOR`: breaking changes
- `MINOR`: backward-compatible features
- `PATCH`: backward-compatible fixes and polish

Example progression:

- `1.0.0`
- `1.1.0`
- `1.1.1`
- `2.0.0`

## Release Flow

Recommended release process:

1. bump the version in `apps/desktop/CMakeLists.txt`
2. commit the version change
3. create and push a matching tag like `v1.0.1`
4. GitHub Actions builds and publishes the release archive

The shipped executable name remains stable across releases:

- `HexMaster.exe`

The version belongs in:

- the Git tag, for example `v1.0.1`
- the release archive name, for example `HexMaster-windows-x64-v1.0.1.zip`
- application metadata and About dialog

Avoid putting the version directly in the executable filename for the normal release package. A stable executable name is cleaner for shortcuts, PATH usage, and end-user installs.

Tagged releases are handled by:

- [.github/workflows/release.yml](.github/workflows/release.yml)

## GitHub Automation

This repository includes:

- CI workflow for Rust tests and Windows Qt build
- release workflow for tagged Windows artifacts
- Pages deployment workflow for the public landing page

Workflow files:

- [.github/workflows/ci.yml](.github/workflows/ci.yml)
- [.github/workflows/release.yml](.github/workflows/release.yml)
- [.github/workflows/pages.yml](.github/workflows/pages.yml)

## GitHub Pages

The public landing page is served from:

- [docs/index.html](docs/index.html)

Pages content is deployed from the `docs/` directory using GitHub Actions.

## Repository Layout

- `crates/hexapp-core`: core document and selection logic
- `crates/hexapp-io`: file-backed I/O support
- `crates/hexapp-search`: search-related logic
- `crates/hexapp-analysis`: hashes and analysis helpers
- `crates/hexapp-inspector`: typed data interpretation
- `crates/hexapp-session`: settings and session persistence
- `crates/hexapp-ffi`: Rust bridge consumed by the Qt shell
- `apps/desktop`: main Qt desktop application
- `apps/bootstrap`: Rust-side bootstrap crate
- `docs/`: GitHub Pages site and public-facing project docs
- `docs/project/`: engineering, roadmap, build, and release documents
- `scripts/`: local bootstrap and build scripts

## Public Repo Checklist

For a professional public repository, the expected baseline is:

- clean root `.gitignore`
- single-source versioning
- license file
- CI on push and pull request
- release automation on tags
- GitHub Pages landing page
- clear build and release instructions

## Documentation

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
