# Export installed vcpkg package versions for release reproducibility.
# Usage: .\scripts\export-vcpkg-versions.ps1 [-OutputPath docs\vcpkg-versions.lock.txt]

param(
    [string]$OutputPath = (Join-Path $PSScriptRoot "..\docs\vcpkg-versions.lock.txt")
)

$ErrorActionPreference = "Stop"

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set. Point it to your vcpkg installation first."
}

$vcpkgExe = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
if (-not (Test-Path $vcpkgExe)) {
    throw "vcpkg.exe not found at $vcpkgExe"
}

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$manifest = Join-Path $root "vcpkg.json"
$header = @(
    "# GIS TOOL vcpkg dependency snapshot",
    "# Generated: $(Get-Date -Format o)",
    "# Triplet: x64-windows",
    "# Manifest: $manifest",
    ""
)

$listOutput = & $vcpkgExe list --triplet x64-windows 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg list failed: $listOutput"
}

$lines = $header + ($listOutput | ForEach-Object { $_.ToString() })
$dir = Split-Path $OutputPath -Parent
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

Set-Content -Path $OutputPath -Value $lines -Encoding UTF8
Write-Host "Wrote $($lines.Count) lines to $OutputPath"
