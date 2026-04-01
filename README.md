# Hex Master

`Hex Master` is a Windows-first, cross-platform hex editor built around a `Rust` core and a `Qt` desktop shell.

The design target is straightforward:

- open very large files quickly
- keep memory usage bounded
- support safe editing without loading the whole file into RAM
- preserve dense, keyboard-first desktop workflows

## Repository Layout

- `crates/hexapp-core`: document model, offsets, selections, edit operations
- `crates/hexapp-io`: file-backed sources, save strategy, large-file I/O primitives
- `crates/hexapp-search`: background search and replace services
- `crates/hexapp-analysis`: hashes and analysis jobs
- `crates/hexapp-inspector`: typed value decoding for the data inspector
- `crates/hexapp-session`: settings and session persistence models
- `crates/hexapp-ffi`: narrow bridge surface between the Rust core and Qt shell
- `ui/qt`: Rust-side desktop bootstrap binary
- `ui/qt-shell`: CMake/Qt desktop shell skeleton
- `docs/`: product and engineering specifications

## Current Status

This repository is scaffolded for `Milestone 0`.

Implemented in the scaffold:

- workspace and crate boundaries
- baseline specs and roadmap
- core domain types for offsets, ranges, selections, bookmarks, and document summaries
- initial file source and search/analysis/inspector/session models
- narrow FFI-facing app state
- Qt shell skeleton with `QMainWindow`, menu bar, status bar, and dock placeholders

Not implemented yet:

- custom hex viewport
- piece table editing model
- save pipeline
- large-file page cache
- background search engine

## Build Notes

### Single Build Command

```powershell
.\scripts\build.ps1
```

This command:

- builds the Rust workspace
- runs the Rust test suite
- configures and builds the Qt shell if `Qt 6` is installed and `qtpaths` is on `PATH`

If Qt is not installed yet, you can still build the Rust side with:

```powershell
.\scripts\build.ps1 -SkipQt
```

### Rust

```powershell
cargo check
```

### Qt Shell

The Qt shell currently lives in `ui/qt-shell` as a CMake project skeleton. It is intentionally decoupled from the Rust build until the FFI boundary is finalized.

Expected local prerequisites:

- `Qt 6`
- `CMake 3.27+`
- a C++20-capable compiler

### Bootstrap Script

```powershell
.\scripts\bootstrap.ps1
```

This script checks for the baseline local toolchain used by the scaffold.

## Docs

- `docs/PRODUCT_SPEC.md`
- `docs/ARCHITECTURE.md`
- `docs/ROADMAP.md`
- `docs/MVP_CHECKLIST.md`
