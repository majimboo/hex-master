# Build Guide

## Supported Build Targets

### Windows

Windows is the primary supported platform for packaged releases.

Requirements:

- Rust toolchain
- CMake 3.27+
- Qt 6 for MSVC
- Visual Studio 2022 or MSVC 2022 build tools

Debug:

```powershell
.\scripts\build.ps1
```

Release:

```powershell
.\scripts\build.ps1 -Configuration Release
```

### Linux

Linux is not currently an officially packaged release target. A source build may be possible with:

- Rust
- CMake 3.27+
- Qt 6 development packages
- a C++20-capable compiler

Additional Linux-specific runtime and deployment work is still required before calling it release-ready.

### macOS

macOS is not currently an officially packaged release target. A source build may become possible later, but the repository does not yet provide validated release packaging for macOS.

## Output Naming

Release packaging policy:

- executable name: `HexMaster.exe`
- release archive name: `HexMaster-windows-x64-vX.Y.Z.zip`

The executable name stays stable. Versioning belongs in the archive, tag, and metadata rather than in the executable filename itself.
