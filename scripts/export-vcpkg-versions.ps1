# Export installed vcpkg package versions for release reproducibility.
# Usage: .\scripts\export-vcpkg-versions.ps1 [-OutputPath docs\vcpkg-versions.lock.txt] [-Triplet x64-windows]

param(
    [string]$OutputPath = (Join-Path $PSScriptRoot "..\docs\vcpkg-versions.lock.txt"),
    [string]$Triplet = "x64-windows"
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
$installedTripletDir = Join-Path $env:VCPKG_ROOT "installed\$Triplet"

function Get-PackageLinesFromShareDir {
    param([string]$ShareDir)

    if (-not (Test-Path $ShareDir)) {
        return @()
    }

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($entry in Get-ChildItem $ShareDir -Directory) {
        $name = $entry.Name
        if ($name -in @("aclocal", "pkgconfig", "man", "doc")) {
            continue
        }

        $version = ""
        $configVersion = Get-ChildItem $entry.FullName -Filter "*ConfigVersion.cmake" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($configVersion) {
            $content = Get-Content $configVersion.FullName -Raw -ErrorAction SilentlyContinue
            if ($content -match 'PACKAGE_VERSION\s+"([^"]+)"') {
                $version = $Matches[1]
            }
        }

        if (-not $version) {
            $abiFile = Join-Path $entry.FullName "vcpkg_abi_info.txt"
            if (Test-Path $abiFile) {
                $version = "installed"
            }
        }

        if ($version) {
            $lines.Add("${name}:${Triplet} ${version}")
        }
    }

    return $lines | Sort-Object -Unique
}

$header = @(
    "# GIS TOOL vcpkg dependency snapshot",
    "# Generated: $(Get-Date -Format o)",
    "# Triplet: $Triplet",
    "# Manifest: $manifest",
    ""
)

$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$listOutput = & $vcpkgExe list --triplet $Triplet 2>&1
$listExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction

$packageLines = @()
$listText = ($listOutput | ForEach-Object { $_.ToString() }) -join "`n"
$looksLikePackageList = $listText -match ':x64-windows\s+\S'
if ($listExitCode -eq 0 -and $looksLikePackageList) {
    $packageLines = $listOutput | ForEach-Object { $_.ToString() } | Where-Object { $_ -and $_ -match ':x64-windows\s+\S' }
} else {
    Write-Warning "vcpkg list unavailable; falling back to scanning $installedTripletDir/share"
    $packageLines = Get-PackageLinesFromShareDir (Join-Path $installedTripletDir "share")
    if ($packageLines.Count -eq 0) {
        throw "Unable to export vcpkg versions: vcpkg list failed and share scan returned no packages."
    }
}

$lines = $header + $packageLines
$dir = Split-Path $OutputPath -Parent
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

Set-Content -Path $OutputPath -Value $lines -Encoding UTF8
Write-Host "Wrote $($packageLines.Count) package lines to $OutputPath"
