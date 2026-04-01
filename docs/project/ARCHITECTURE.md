# Architecture

## Overview

Hex Master is a Windows-first desktop hex editor built as a Qt application over a Rust core.

- Qt owns the desktop shell: menus, docks, dialogs, status bar, search panels, inspector presentation, and the custom hex view widget.
- Rust owns the document state, editing operations, file persistence, search helpers, inspector decoding, checksums, and session-oriented state exposed through the FFI bridge.
- The bridge stays intentionally narrow so the UI can remain native without duplicating editing logic.

## Repository Layout

- `apps/desktop`: Qt desktop application
- `apps/bootstrap`: small Rust bootstrap crate
- `crates/hexapp-core`: shared document and selection primitives
- `crates/hexapp-io`: file-backed document I/O and save handling
- `crates/hexapp-search`: search query and replace helpers
- `crates/hexapp-analysis`: checksum and hash helpers
- `crates/hexapp-inspector`: typed interpretation helpers
- `crates/hexapp-session`: recent files and session persistence
- `crates/hexapp-ffi`: Rust bridge consumed by the desktop app

## Desktop Layer

The desktop app lives in `apps/desktop` and is built around a custom `HexView` widget rather than a generic table control.

Current desktop responsibilities:

- viewport layout and painting
- offset-to-cell hit testing
- synchronized hex and text panes
- caret and selection rendering
- insert versus overwrite interaction
- menus, toolbars, dialogs, and docks
- search results table and navigation
- about box, settings, recent files, and session restore

## Rust Layer

The Rust side is split into focused crates, but today the most important public surface is the FFI document handle used by the desktop shell.

Current Rust responsibilities:

- open existing files and create new buffers
- read ranges for the visible viewport
- overwrite, insert, and delete byte ranges
- undo and redo sequencing
- save and save-as flows
- typed search pattern generation
- checksum and hash computation
- inspector decoding for integer, float, and time views

## Current Document Model

The editor now supports structural insert and delete operations, but it is not yet the final large-file editing architecture.

Current behavior:

- file-backed read access through the Rust layer
- mutable in-memory editing state exposed through FFI
- overwrite, insert, delete, cut, paste, fill, and replace operations
- safer same-path save flow using a temporary output and replacement step

Current limitation:

- the implementation is functional for normal editing workflows, but it is not yet a piece-table or similarly scalable edit overlay tuned for very large structural edit workloads

## Search and Analysis

Current search features:

- raw hex-byte search
- text search with multiple encodings
- typed-value search for integer and floating-point values
- replace next and replace all
- search-in-selection scope
- results table with offsets, previews, and range navigation

Current analysis features:

- CRC32
- MD5
- SHA-1
- SHA-256

Current limitation:

- long-running search and analysis work still uses synchronous execution with visible busy feedback rather than fully asynchronous background jobs with progress and cancel

## Save and Recovery Model

Current save expectations:

- save and save-as are supported
- same-path save uses a temp-file replacement flow instead of deleting the original first
- dirty-state prompts protect against losing unsaved changes when opening, creating, or closing documents

Still missing:

- crash recovery and autosave
- broader release-grade fault-injection and stress testing

## Versioning and Release Metadata

Application version is defined once in `apps/desktop/CMakeLists.txt`.

That version feeds:

- Qt application metadata
- About dialog version display
- Windows executable version information
- release archive naming in GitHub automation
