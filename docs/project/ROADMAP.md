# Roadmap

## Current Status

Hex Master has moved past the scaffold stage. The repository now contains a functional Windows desktop application with versioning, CI, release automation, and a public Pages site.

The roadmap below focuses on the work still worth doing after the current 1.0 baseline.

## 1.0 Baseline

Already in place:

- Qt desktop shell over a Rust backend
- hex and text pane editing
- insert and overwrite modes
- structural insert, delete, cut, and paste
- search, replace, and search results navigation
- inspector and checksum/hash docks
- recent files, settings, session restore, and About dialog
- unified versioning and GitHub release infrastructure

## Next Milestone: Responsiveness

Priority work:

- move long-running search work off the UI thread
- move hash and analysis work off the UI thread
- add progress reporting and cancellation
- avoid visible UI stalls during large replace-all and analysis operations

Exit criteria:

- large searches and hashes no longer feel like the app is frozen
- the UI provides progress or at least a cancelable job state for heavy work

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
- release signing strategy
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

## Longer-Term Exploration

Possible future features after the 1.x stability work:

- binary diff
- entropy and histogram views
- annotations
- templates
- scripting or plugin hooks

These remain optional product-expansion work, not prerequisites for cleaning up the current release line.
