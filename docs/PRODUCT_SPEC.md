# Product Spec

## Goal

Build a modern desktop hex editor with the speed and large-file handling expected from classic native tools, while using a safer and more maintainable architecture.

## Product Principles

- Open huge files without loading the whole file into memory.
- Keep scrolling, selection, and navigation responsive.
- Make editing safe and crash-resistant.
- Preserve dense, keyboard-first desktop workflows.
- Keep the core portable and the UI native.

## Target Users

- Reverse engineers
- Embedded developers
- File format developers
- Security researchers
- Power users working directly with binary data

## V1 Scope

- Open existing files and create new buffers
- Read-only mode
- Insert and overwrite modes
- Hex pane plus ASCII/text pane
- Offset gutter
- Selection and caret navigation
- Undo and redo
- Copy, cut, paste
- Paste from parsed hex text
- Fill selected range
- Go to offset
- Bookmarks
- Find and replace
- Text search with multiple encodings
- Data inspector
- Checksums and hashes
- Recent files
- Settings and session restore
- Crash-safe save flow

## Deferred Beyond V1

- Binary diff
- Entropy and histogram views
- Binary templates
- Scripting
- Plugin API
- Device and process memory editing

## Performance Targets

- Initial viewport should become interactive without a full-file read.
- Memory usage must scale with viewport cache, edit history, and UI state rather than file size.
- Multi-GB files must remain navigable and searchable without freezing the UI.
- Public document offsets must use `u64`.

## Large File Strategy

- File-backed document source
- Optional `mmap` path for read-heavy access
- Paged read fallback with LRU cache
- Piece-table style edit overlay
- Background search and analysis jobs
- Temp-file save and atomic replace where possible
