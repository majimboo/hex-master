# Roadmap

## Milestone 0: Foundation

- workspace and crate boundaries
- Qt main window shell
- core domain types
- initial bridge layer
- documentation set

Exit criteria:

- repository builds with `cargo check`
- shell skeleton is present
- product and architecture decisions are documented

## Milestone 1: Read-Only Browser

- open file flow
- file metadata
- visible range reads
- custom hex viewport skeleton
- navigation, selection, go-to-offset
- bookmarks

Exit criteria:

- browse large files comfortably in read-only mode

## Milestone 2: Editing Core

- piece table
- insert, overwrite, delete, replace
- undo/redo
- clipboard support
- dirty state tracking

Exit criteria:

- large files can be edited without whole-file RAM residency

## Milestone 3: Save and Hardening

- save/save as
- temp-file replacement
- recovery-oriented error handling
- integration tests for save correctness

## Milestone 4: Search and Replace

- byte pattern search
- text search in multiple encodings
- results list
- replace current/all
- cancelable background jobs

## Milestone 5: V1 Polish

- inspector
- hashes
- settings
- session restore
- shortcut polish
- packaging

## Milestone 6: V1.5

- binary diff
- histogram
- entropy view
- export/import helpers
- annotations

## Milestone 7: V2

- templates
- scripting
- format-aware inspectors

## Milestone 8: V3

- plugin API
- advanced workflows
- optional device/process features
