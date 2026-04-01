$ErrorActionPreference = "Stop"

function Test-Command {
    param([string]$Name)

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        Write-Host ("[missing] {0}" -f $Name)
        return $false
    }

    Write-Host ("[ok] {0} -> {1}" -f $Name, $cmd.Source)
    return $true
}

function Find-QtPaths {
    $qtpaths = Get-Command "qtpaths" -ErrorAction SilentlyContinue
    if ($null -ne $qtpaths) {
        return $qtpaths.Source
    }

    $defaultRoot = "C:\Qt"
    if (-not (Test-Path $defaultRoot)) {
        return $null
    }

    $candidate = Get-ChildItem -Path $defaultRoot -Filter qtpaths.exe -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if ($null -ne $candidate) {
        return $candidate.FullName
    }

    return $null
}

$allGood = $true

$allGood = (Test-Command "cargo") -and $allGood
$allGood = (Test-Command "rustc") -and $allGood
$allGood = (Test-Command "cmake") -and $allGood

$qtpaths = Find-QtPaths
if ($null -eq $qtpaths) {
    Write-Host "[missing] qtpaths"
    $allGood = $false
} else {
    Write-Host ("[ok] qtpaths -> {0}" -f $qtpaths)
}

if ($allGood) {
    Write-Host "Bootstrap prerequisites look available."
    exit 0
}

Write-Host "One or more required tools are missing."
exit 1
