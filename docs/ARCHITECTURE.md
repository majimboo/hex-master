# Architecture

## High-Level Shape

The application is split into a Rust core and a Qt desktop shell.

- Rust owns document state, editing, file I/O, search, analysis, and typed decoding.
- Qt owns rendering, input, menus, docks, dialogs, and platform-native desktop behavior.
- The bridge between them stays narrow.

## Rust Modules

### `hexapp-core`

- byte offsets and ranges
- selection and caret model
- bookmarks
- document summary and lifecycle state
- edit command model

### `hexapp-io`

- file source abstraction
- metadata and open strategy
- paged reads
- future page cache and save pipeline

### `hexapp-search`

- byte pattern queries
- text queries by encoding
- replace operations
- background search job state

### `hexapp-analysis`

- hash requests and results
- background analysis job model

### `hexapp-inspector`

- typed value decoding
- endianness
- inspector rows shown in the UI

### `hexapp-session`

- recent file list
- window/session settings

### `hexapp-ffi`

- Rust-owned app state exposed to the desktop shell
- thin API for opening documents, listing recent files, and reporting summaries

## Document Model

The long-term document model is:

- immutable base source
- append-only add buffer
- piece descriptors that reference either the base file or add buffer

That allows efficient inserts, deletes, replaces, and undo operations without materializing the entire document in memory.

## Read Path

- read-only access may use `mmap` when advantageous
- cross-platform fallback uses paged reads
- visible viewport requests go through a byte-range API

## Save Path

- write to temporary output
- flush and validate
- replace original atomically when the platform supports it

## UI Model

The central hex viewport is a custom widget rather than a table/grid control.

It is responsible for:

- row and column layout
- offset-to-cell mapping
- cell-to-offset hit testing
- selection painting
- caret painting
- synchronized hex and ASCII panes
- smooth scrolling across very large logical row counts
