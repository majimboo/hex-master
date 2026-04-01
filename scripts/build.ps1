param(
    [switch]$SkipTests,
    [switch]$SkipQt
)

$ErrorActionPreference = "Stop"

function Require-Command {
    param([string]$Name)

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        throw "Required command not found: $Name"
    }

    return $cmd
}

function Invoke-Step {
    param(
        [string]$Description,
        [scriptblock]$Action
    )

    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "Step failed: $Description"
    }
}

function Resolve-QtPrefix {
    $qtpaths = Get-Command "qtpaths" -ErrorAction SilentlyContinue
    if ($null -eq $qtpaths) {
        $defaultRoot = "C:\Qt"
        if (Test-Path $defaultRoot) {
            $candidate = Get-ChildItem -Path $defaultRoot -Filter qtpaths.exe -Recurse -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending |
                Select-Object -First 1

            if ($null -ne $candidate) {
                $qtpaths = @{ Source = $candidate.FullName }
            }
        }
    }

    if ($null -eq $qtpaths) {
        return $null
    }

    $prefix = & $qtpaths.Source --query QT_INSTALL_PREFIX 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "qtpaths was found but QT_INSTALL_PREFIX could not be queried."
    }

    $prefix = $prefix.Trim()
    if ([string]::IsNullOrWhiteSpace($prefix)) {
        throw "qtpaths returned an empty QT_INSTALL_PREFIX."
    }

    return $prefix
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$qtShellSource = Join-Path $repoRoot "ui\\qt-shell"
$qtBuildDir = Join-Path $repoRoot "build\\qt-shell"

Require-Command "cargo" | Out-Null
Require-Command "cmake" | Out-Null

Write-Host "[1/3] Building Rust workspace"
Invoke-Step "cargo build --workspace" { cargo build --workspace }

if (-not $SkipTests) {
    Write-Host "[2/3] Running Rust tests"
    Invoke-Step "cargo test --workspace" { cargo test --workspace }
} else {
    Write-Host "[2/3] Skipping Rust tests"
}

if ($SkipQt) {
    Write-Host "[3/3] Skipping Qt shell build by request"
    exit 0
}

$qtPrefix = Resolve-QtPrefix
if ($null -eq $qtPrefix) {
    throw "Qt build requested, but 'qtpaths' is not available on PATH. Install Qt 6 and add qtpaths to PATH, or rerun with -SkipQt."
}

New-Item -ItemType Directory -Force -Path $qtBuildDir | Out-Null
$qt6Dir = Join-Path $qtPrefix "lib\\cmake\\Qt6"

Write-Host "[3/3] Configuring Qt shell"
Invoke-Step "cmake configure" {
    cmake -S $qtShellSource -B $qtBuildDir -G "Visual Studio 17 2022" -A x64 "-DQt6_DIR=$qt6Dir"
}

Write-Host "[4/4] Building Qt shell"
Invoke-Step "cmake build" { cmake --build $qtBuildDir --config Debug }
