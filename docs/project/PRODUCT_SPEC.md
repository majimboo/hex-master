# Product Spec

## Product Definition

Hex Master is a desktop hex editor for inspecting and editing binary data on Windows, with a Rust core and a Qt user interface.

It is positioned as a modern native alternative to older Windows hex editors such as Hex Workshop for workflows involving:

- executable inspection
- firmware and ROM editing
- save-file and asset editing
- reverse-engineering support work
- direct byte-level patching of custom formats

## Product Goals

- Keep the editor responsive for real desktop workflows.
- Support both hex-side and text-side editing.
- Make structural edits possible, not only fixed-length overwrites.
- Preserve dense keyboard-first workflows expected from classic native editors.
- Keep the implementation maintainable by separating native UI concerns from core editing logic.

## Target Users

- reverse engineers
- embedded and firmware developers
- file-format developers
- security researchers
- technical users who inspect or patch binary files directly

## Current Release Scope

The current release line is centered on a functional Windows desktop editor with these capabilities:

- open existing files and create new buffers
- save, save as, and read-only aware workflows
- hex pane and text pane editing
- insert and overwrite modes
- structural insert, delete, cut, and paste
- undo and redo
- go-to-offset and bookmark navigation
- recent files, persistent settings, and session restore
- inspector views for integer, floating-point, and time interpretations
- checksum and hash tools
- search by text, hex bytes, and typed values
- replace next and replace all
- search results table with result navigation

## Public Positioning

What the project should claim publicly today:

- functional Windows desktop hex editor
- modern alternative in spirit to classic native Windows hex tools
- Rust core with Qt desktop shell
- source available, with automated Windows builds and tagged releases

What the project should not overclaim yet:

- fully proven large-file editing architecture on par with mature commercial editors
- non-blocking background execution for every heavy operation
- installer-based Windows distribution
- cross-platform packaged releases

## Non-Goals for 1.x

These are intentionally outside the immediate release line:

- binary diff/merge workflows
- entropy and histogram visualizations
- binary templates
- scripting
- plugin API
- device memory or live process editing

## Quality Expectations

The public 1.x line should meet these baseline expectations:

- no silent data loss in normal save/open/close flows
- clear dirty-state prompts
- stable executable naming and version metadata
- searchable and buildable public repository
- tagged release artifacts published through GitHub

## Remaining Product Gaps

The main gaps still worth calling out in planning documents are:

- background worker jobs with progress and cancel for long searches and analysis
- more scalable structural editing architecture for very large edit sessions
- installer packaging and signing
- crash recovery and autosave
- broader QA and release hardening
