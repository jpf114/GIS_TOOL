param(
    [string]$CliPath = "",
    [string]$WorkspaceRoot = "",
    [string]$OutputRoot = "",
    [ValidateSet("quick", "full")]
    [string]$Mode = "quick"
)

$ErrorActionPreference = "Stop"
if ($null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Get-DefaultWorkspaceRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-DefaultCliPath {
    param([string]$Root)
    $debugCli = Join-Path $Root "build\debug\src\cli\Debug\gis-cli.exe"
    if (Test-Path $debugCli) {
        return $debugCli
    }
    return Join-Path $Root "build\release\src\cli\Release\gis-cli.exe"
}

function Get-DefaultOutputRoot {
    param([string]$Root)
    return Join-Path $Root "tmp\vector_regression"
}

function Require-Path {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path $Path)) {
        throw ($Label + " missing: " + $Path)
    }
}

function Sync-PluginDlls {
    param([string]$ResolvedCliPath)

    $cliDir = Split-Path $ResolvedCliPath -Parent
    $pluginDir = Join-Path $cliDir "plugins"
    New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null

    $buildRoot = Resolve-Path (Join-Path $cliDir "..\..\..")
    $configName = Split-Path $cliDir -Leaf
    $pluginBuildRoot = Join-Path $buildRoot "src\plugins"
    $pluginDlls = @()

    if (Test-Path $pluginBuildRoot) {
        $pluginDlls = Get-ChildItem $pluginBuildRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $candidate = Join-Path $_.FullName $configName
                if (Test-Path $candidate) {
                    Get-ChildItem $candidate -Filter "plugin_*.dll" -ErrorAction SilentlyContinue
                }
            }
    }

    if ($pluginDlls.Count -eq 0) {
        $pluginDlls = Get-ChildItem (Join-Path $buildRoot "plugins") -Recurse -Filter "plugin_*.dll" -ErrorAction SilentlyContinue
    }

    foreach ($dll in $pluginDlls) {
        Copy-Item $dll.FullName $pluginDir -Force
    }
}

function New-Case {
    param(
        [string]$Name,
        [string[]]$CaseArgs,
        [string[]]$ExpectedOutputs
    )

    return [pscustomobject]@{
        Name = $Name
        Args = [string[]]$CaseArgs
        ExpectedOutputs = [string[]]$ExpectedOutputs
    }
}

function Invoke-Case {
    param(
        [string]$ResolvedCliPath,
        [pscustomobject]$Case
    )

    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $arguments = @()
    foreach ($arg in [string[]]$Case.Args) {
        if ($null -ne $arg -and $arg -ne "") {
            $arguments += $arg
        }
    }

    $stdoutPath = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString() + "_gis_stdout.txt")
    $stderrPath = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString() + "_gis_stderr.txt")

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $ResolvedCliPath @arguments 1> $stdoutPath 2> $stderrPath
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorAction
    $stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { "" }
    $stderr = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { "" }
    Remove-Item $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    $output = (($stdout + [Environment]::NewLine + $stderr).Trim())
    $watch.Stop()

    $result = [pscustomobject]@{
        Name = $Case.Name
        ExitCode = $code
        Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
        Output = ($output | Out-String).Trim()
    }

    if ($code -ne 0) {
        throw ($Case.Name + " failed`n" + $result.Output)
    }

    foreach ($outputPath in [string[]]$Case.ExpectedOutputs) {
        if (-not (Test-Path $outputPath)) {
            throw ($Case.Name + " did not produce output: " + $outputPath)
        }
        $item = Get-Item $outputPath
        if (-not $item.PSIsContainer -and $item.Length -le 0) {
            throw ($Case.Name + " produced empty file: " + $outputPath)
        }
    }

    return $result
}

function Save-RegressionResults {
    param(
        [string]$ResolvedOutputRoot,
        [string]$Mode,
        [object[]]$Results
    )

    $jsonPath = Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".json")
    $csvPath = Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".csv")

    $payload = [pscustomobject]@{
        mode = $Mode
        generated_at = (Get-Date).ToString("s")
        output_root = $ResolvedOutputRoot
        results = $Results
    }

    $payload | ConvertTo-Json -Depth 6 | Set-Content -Path $jsonPath -Encoding UTF8
    $Results | Select-Object Name,ExitCode,Seconds | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8
}

function Invoke-SetupCommand {
    param(
        [string]$ResolvedCliPath,
        [string]$Name,
        [string[]]$CaseArgs
    )

    $case = [pscustomobject]@{
        Name = $Name
        Args = [string[]]$CaseArgs
    }

    return Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case | Out-Null
}

function Ensure-BenchmarkData {
    param(
        [string]$ResolvedCliPath,
        [string]$ResolvedWorkspaceRoot
    )

    $benchmarkRoot = Join-Path $ResolvedWorkspaceRoot "tmp\vector_benchmark"
    New-Item -ItemType Directory -Force -Path $benchmarkRoot | Out-Null

    $vectorDataRoot = Join-Path $ResolvedWorkspaceRoot "data\vector"
    $roadsRaw = $null
    $chinaRaw = $null

    if (Test-Path $vectorDataRoot) {
        $roadsRawFile = Get-ChildItem $vectorDataRoot -Filter "*.shp" |
            Sort-Object Length |
            Select-Object -First 1
        $chinaRawFile = Get-ChildItem $vectorDataRoot -Filter "*.geojson" |
            Select-Object -First 1
        if ($null -ne $roadsRawFile) {
            $roadsRaw = $roadsRawFile.FullName
        }
        if ($null -ne $chinaRawFile) {
            $chinaRaw = $chinaRawFile.FullName
        }
    }

    if ([string]::IsNullOrWhiteSpace($roadsRaw) -or [string]::IsNullOrWhiteSpace($chinaRaw)) {
        $sampleDataRoot = Join-Path $benchmarkRoot "generated_data"
        New-Item -ItemType Directory -Force -Path $sampleDataRoot | Out-Null

        $roadsRaw = Join-Path $sampleDataRoot "roads_sample.geojson"
        $chinaRaw = Join-Path $sampleDataRoot "china_sample.geojson"

        @'
{
  "type": "FeatureCollection",
  "name": "roads_sample",
  "crs": {
    "type": "name",
    "properties": {
      "name": "EPSG:4326"
    }
  },
  "features": [
    {
      "type": "Feature",
      "properties": {
        "name": "road_primary",
        "fclass": "primary"
      },
      "geometry": {
        "type": "LineString",
        "coordinates": [
          [116.20, 39.60],
          [116.55, 39.92],
          [116.95, 40.20]
        ]
      }
    },
    {
      "type": "Feature",
      "properties": {
        "name": "road_secondary",
        "fclass": "secondary"
      },
      "geometry": {
        "type": "LineString",
        "coordinates": [
          [115.60, 40.00],
          [116.40, 40.05],
          [117.20, 40.10]
        ]
      }
    },
    {
      "type": "Feature",
      "properties": {
        "name": "road_residential",
        "fclass": "residential"
      },
      "geometry": {
        "type": "LineString",
        "coordinates": [
          [118.20, 39.10],
          [118.50, 39.30]
        ]
      }
    }
  ]
}
'@ | Set-Content -Path $roadsRaw -Encoding UTF8

        @'
{
  "type": "FeatureCollection",
  "name": "china_sample",
  "crs": {
    "type": "name",
    "properties": {
      "name": "EPSG:4326"
    }
  },
  "features": [
    {
      "type": "Feature",
      "properties": {
        "name": "north_china_focus"
      },
      "geometry": {
        "type": "Polygon",
        "coordinates": [
          [
            [115.00, 39.20],
            [117.80, 39.20],
            [117.80, 41.20],
            [115.00, 41.20],
            [115.00, 39.20]
          ]
        ]
      }
    }
  ]
}
'@ | Set-Content -Path $chinaRaw -Encoding UTF8
    }

    $roadsCore = Join-Path $benchmarkRoot "bj_roads_core.gpkg"
    $roadsCore3857 = Join-Path $benchmarkRoot "bj_roads_core_3857.gpkg"
    $chinaFocus = Join-Path $benchmarkRoot "china_focus.gpkg"
    $chinaBbox = Join-Path $benchmarkRoot "china_bbox.geojson"
    $chinaBbox3857 = Join-Path $benchmarkRoot "china_bbox_3857.gpkg"

    Require-Path -Path $roadsRaw -Label "raw roads"
    Require-Path -Path $chinaRaw -Label "raw china"

    if (-not (Test-Path $roadsCore)) {
        Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_roads_core" -CaseArgs @(
            "vector", "filter",
            ("--input=" + $roadsRaw),
            ("--output=" + $roadsCore),
            "--where=fclass IN ('motorway','trunk','primary','secondary')"
        )
    }

    if (-not (Test-Path $chinaFocus)) {
        Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_china_focus" -CaseArgs @(
            "vector", "filter",
            ("--input=" + $chinaRaw),
            ("--output=" + $chinaFocus),
            "--extent=115.30,39.35,117.60,41.10"
        )
    }

    if (-not (Test-Path $chinaBbox)) {
        @'
{
  "type": "FeatureCollection",
  "name": "beijing_bbox",
  "crs": {
    "type": "name",
    "properties": {
      "name": "EPSG:4326"
    }
  },
  "features": [
    {
      "type": "Feature",
      "properties": {
        "name": "beijing_bbox"
      },
      "geometry": {
        "type": "Polygon",
        "coordinates": [
          [
            [115.30, 39.35],
            [117.60, 39.35],
            [117.60, 41.10],
            [115.30, 41.10],
            [115.30, 39.35]
          ]
        ]
      }
    }
  ]
}
'@ | Set-Content -Path $chinaBbox -Encoding UTF8
    }

    if (-not (Test-Path $roadsCore3857)) {
        Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_roads_core_3857" -CaseArgs @(
            "projection", "reproject",
            ("--input=" + $roadsCore),
            ("--output=" + $roadsCore3857),
            "--dst_srs=EPSG:3857"
        )
    }

    if (-not (Test-Path $chinaBbox3857)) {
        Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_china_bbox_3857" -CaseArgs @(
            "projection", "reproject",
            ("--input=" + $chinaBbox),
            ("--output=" + $chinaBbox3857),
            "--dst_srs=EPSG:3857"
        )
    }
}

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = Get-DefaultWorkspaceRoot
}
$ResolvedWorkspaceRoot = (Resolve-Path $WorkspaceRoot).Path

if ([string]::IsNullOrWhiteSpace($CliPath)) {
    $CliPath = Get-DefaultCliPath -Root $ResolvedWorkspaceRoot
}
$ResolvedCliPath = (Resolve-Path $CliPath).Path
Require-Path -Path $ResolvedCliPath -Label "gis-cli"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Get-DefaultOutputRoot -Root $ResolvedWorkspaceRoot
}
$ResolvedOutputRoot = $OutputRoot
New-Item -ItemType Directory -Force -Path $ResolvedOutputRoot | Out-Null

Sync-PluginDlls -ResolvedCliPath $ResolvedCliPath
Ensure-BenchmarkData -ResolvedCliPath $ResolvedCliPath -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot

$benchmarkRoot = Join-Path $ResolvedWorkspaceRoot "tmp\vector_benchmark"
$roadsCore = Join-Path $benchmarkRoot "bj_roads_core.gpkg"
$roadsCore3857 = Join-Path $benchmarkRoot "bj_roads_core_3857.gpkg"
$chinaBbox = Join-Path $benchmarkRoot "china_bbox.geojson"
$chinaBbox3857 = Join-Path $benchmarkRoot "china_bbox_3857.gpkg"

Require-Path -Path $roadsCore -Label "roads core"
Require-Path -Path $roadsCore3857 -Label "roads core 3857"
Require-Path -Path $chinaBbox -Label "china bbox"
Require-Path -Path $chinaBbox3857 -Label "china bbox 3857"

$cases = @()
$cases += New-Case -Name "buffer_quick" -CaseArgs @(
    "vector", "buffer",
    ("--input=" + $roadsCore3857),
    ("--output=" + (Join-Path $ResolvedOutputRoot "buffer_quick.gpkg")),
    "--distance=100"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "buffer_quick.gpkg")
)
$cases += New-Case -Name "clip_focus" -CaseArgs @(
    "vector", "clip",
    ("--input=" + $roadsCore),
    ("--clip_vector=" + $chinaBbox),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "clip_focus.gpkg")
)
$cases += New-Case -Name "difference_focus" -CaseArgs @(
    "vector", "difference",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $chinaBbox),
    ("--output=" + (Join-Path $ResolvedOutputRoot "difference_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "difference_focus.gpkg")
)
$cases += New-Case -Name "union_focus" -CaseArgs @(
    "vector", "union",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $chinaBbox),
    ("--output=" + (Join-Path $ResolvedOutputRoot "union_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "union_focus.gpkg")
)
$cases += New-Case -Name "dissolve_bbox" -CaseArgs @(
    "vector", "dissolve",
    ("--input=" + $chinaBbox3857),
    ("--output=" + (Join-Path $ResolvedOutputRoot "dissolve_bbox.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "dissolve_bbox.gpkg")
)
$cases += New-Case -Name "convert_roads_geojson" -CaseArgs @(
    "vector", "convert",
    ("--input=" + $roadsCore),
    ("--output=" + (Join-Path $ResolvedOutputRoot "roads_core.geojson")),
    "--format=GeoJSON"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "roads_core.geojson")
)
$rasterizeOutput = Join-Path $ResolvedOutputRoot "bbox_rasterized.tif"
$cases += New-Case -Name "rasterize_bbox" -CaseArgs @(
    "vector", "rasterize",
    ("--input=" + $chinaBbox),
    ("--output=" + $rasterizeOutput),
    "--resolution=0.05"
) -ExpectedOutputs @(
    $rasterizeOutput
)
$cases += New-Case -Name "polygonize_bbox_raster" -CaseArgs @(
    "vector", "polygonize",
    ("--input=" + $rasterizeOutput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "bbox_polygonized.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "bbox_polygonized.gpkg")
)

if ($Mode -eq "full") {
    $cases += New-Case -Name "clip_stress" -CaseArgs @(
        "vector", "clip",
        ("--input=" + $roadsCore),
        ("--clip_vector=" + $chinaBbox),
        ("--output=" + (Join-Path $ResolvedOutputRoot "clip_stress.gpkg"))
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "clip_stress.gpkg")
    )
    $cases += New-Case -Name "difference_stress" -CaseArgs @(
        "vector", "difference",
        ("--input=" + $roadsCore),
        ("--overlay_vector=" + $chinaBbox),
        ("--output=" + (Join-Path $ResolvedOutputRoot "difference_stress.gpkg"))
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "difference_stress.gpkg")
    )
}

$results = @()
foreach ($case in $cases) {
    $results += Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
}

Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Vector regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(20) + " " + $result.Seconds + "s")
}
Write-Host ("  summary_json".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".json")))
Write-Host ("  summary_csv".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".csv")))
