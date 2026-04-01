# Roadmap

## Current Status

Hex Master has moved past the scaffold stage. The repository now contains a functional Windows desktop application with versioning, CI, release automation, and a public Pages site.

The roadmap below is the current source of truth for remaining product and release work.

## Current Baseline

Already in place:

- Qt desktop shell over a Rust backend
- hex and text pane editing
- insert and overwrite modes
- structural insert, delete, cut, and paste
- search, replace, and search results navigation
- inspector editing and checksum/hash docks
- configurable viewport layout with persisted view state
- recent files, settings, session restore, and About dialog
- unified versioning and GitHub release infrastructure
- schema editor tool window with structure parsing, progress, and coverage reporting
- compare tool window with side-by-side views, results, progress, and diff navigation

## Near-Term Priorities

Highest-value remaining work:

- move hash and remaining analysis work off the UI thread
- improve progress and cancel consistency for the remaining heavy operations
- improve large-file structural editing scalability
- add installer packaging for Windows releases
- add code signing for public Windows builds
- add crash recovery and autosave
- broaden release QA for large-file, replace, save, and compare scenarios

## Next Milestone: Responsiveness and Long Operations

Priority work:

- move hash and analysis work off the UI thread
- extend background-job architecture beyond compare and schema execution
- add cleaner cancel/progress behavior for remaining heavy operations
- avoid visible UI stalls during large replace-all and analysis operations

Exit criteria:

- large hashes and remaining heavy operations no longer feel like the app is frozen
- the UI provides progress or a cancelable job state for heavy work consistently

## Following Milestone: Editing Scalability

Priority work:

- improve the structural editing model for very large files
- reduce full-buffer materialization pressure after repeated insert/delete operations
- harden undo/redo behavior under larger edit sessions

Exit criteria:

- repeated structural edits remain practical on larger inputs
- memory growth is better bounded during long edit sessions

## Following Milestone: Distribution Hardening

Priority work:

- Windows installer packaging
- code signing strategy
- cleaner runtime dependency bundling
- clearer release notes generated from the changelog

Exit criteria:

- public Windows releases are easy to install and update
- release assets and metadata look professional

## Following Milestone: Reliability

Priority work:

- crash recovery and autosave
- more integration coverage around save correctness and replace workflows
- edge-case QA for read-only, encoding, and large-file scenarios

Exit criteria:

- recovery expectations are documented and implemented
- release confidence is based on broader automated and manual coverage

## Longer-Term Product Work

Possible future features after the 1.x stability work:

- aligned binary diff with insert/delete-aware compare
- export parsed schema structures to JSON or similar interchange formats
- entropy and histogram views
- annotations
- richer schema tooling or visual structure builders
- scripting or plugin hooks

These remain optional product-expansion work, not prerequisites for cleaning up the current release line.
