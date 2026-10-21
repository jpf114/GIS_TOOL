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

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-CliAndCaptureText {
    param(
        [string]$ResolvedCliPath,
        [string[]]$Arguments
    )

    $stdoutPath = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString() + "_gis_stdout.txt")
    $stderrPath = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString() + "_gis_stderr.txt")
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $ResolvedCliPath @Arguments 1> $stdoutPath 2> $stderrPath
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorAction
    $stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { "" }
    $stderr = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { "" }
    Remove-Item $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    Assert-Condition -Condition ($code -eq 0) -Message ("CLI validation command failed: " + ($Arguments -join " "))
    return (($stdout + [Environment]::NewLine + $stderr).Trim())
}

function Assert-TextContains {
    param(
        [string]$Text,
        [string]$Expected,
        [string]$Message
    )

    Assert-Condition -Condition ($Text.Contains($Expected)) -Message $Message
}

function Get-ReportedCount {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Label
    )

    $match = [regex]::Match($Text, $Pattern)
    Assert-Condition -Condition ($match.Success) -Message ($Label + ": pattern '" + $Pattern + "' matched nothing; raw output was: " + $Text)
    return [int]$match.Groups[1].Value
}

function Assert-CountEquals {
    param(
        [string]$Text,
        [string]$Pattern,
        [int]$Expected,
        [string]$Label
    )

    $actual = Get-ReportedCount -Text $Text -Pattern $Pattern -Label $Label
    Assert-Condition -Condition ($actual -eq $Expected) -Message ($Label + ": reported " + $actual + ", expected " + $Expected)
}

function Format-Coord {
    param([double]$Value)

    return $Value.ToString("0.######", [System.Globalization.CultureInfo]::InvariantCulture)
}

function Write-BboxGeojson {
    param(
        [string]$Path,
        [double[]]$Box,
        [string]$Label
    )

    $x0 = Format-Coord -Value $Box[0]
    $y0 = Format-Coord -Value $Box[1]
    $x1 = Format-Coord -Value $Box[2]
    $y1 = Format-Coord -Value $Box[3]
    $ring = "[[$x0, $y0], [$x1, $y0], [$x1, $y1], [$x0, $y1], [$x0, $y0]]"

    $json = '{"type":"FeatureCollection","name":"' + $Label + '","crs":{"type":"name","properties":{"name":"EPSG:4326"}},"features":[{"type":"Feature","properties":{"name":"' + $Label + '"},"geometry":{"type":"Polygon","coordinates":[' + $ring + ']}}]}'
    Set-Content -Path $Path -Value $json -Encoding UTF8
}

function Get-LayerStats {
    param(
        [string]$ResolvedCliPath,
        [string]$Path,
        [string]$ScratchDir
    )

    # vector info 目前被插件级 output 必填挡住（漂移清单 D1），所以这里必须带一个占位输出；
    # doInfo 不写文件，占位路径只是用来满足校验。
    $text = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
        "vector", "info",
        ("--input=" + $Path),
        ("--output=" + (Join-Path $ScratchDir "layer_stats_placeholder.gpkg"))
    )

    $count = [regex]::Match($text, "Features:\s+(\d+)")
    Assert-Condition -Condition ($count.Success) -Message ("layer stats: no Features line in info output for " + $Path)
    $extent = [regex]::Match($text, "Extent:\s+(-?[0-9]+(?:\.[0-9]+)?),\s*(-?[0-9]+(?:\.[0-9]+)?)\s+-\s+(-?[0-9]+(?:\.[0-9]+)?),\s*(-?[0-9]+(?:\.[0-9]+)?)")
    Assert-Condition -Condition ($extent.Success) -Message ("layer stats: no 'Extent: minx, miny - maxx, maxy' line in info output for " + $Path)

    return [pscustomobject]@{
        FeatureCount = [int]$count.Groups[1].Value
        MinX = [double]$extent.Groups[1].Value
        MinY = [double]$extent.Groups[2].Value
        MaxX = [double]$extent.Groups[3].Value
        MaxY = [double]$extent.Groups[4].Value
    }
}

function Validate-CaseOutputs {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [string]$ResolvedOutputRoot,
        [int]$InputFeatureCount
    )

    # 期望值一律由输入图层自己的要素数推出，不写死常量。
    # WHY：本脚本自 2026-04-27 起优先用 data/vector 里的真实道路（现 22836 要素），
    # 而旧常量按 3 要素合成样本标定 ⇒ clip/union/intersect/spatial_join 长期红，
    # buffer_quick 则因 "feature_count: 2" 恰是 "feature_count: 22836" 的前缀而假绿。
    switch ($Case.Name) {
        "buffer_quick" {
            Assert-CountEquals -Text $Case.Output -Pattern "feature_count: (\d+)" -Expected $InputFeatureCount -Label "buffer_quick"
            Assert-TextContains -Text $Case.Output -Expected "srs_type: projected" -Message "buffer_quick srs_type mismatch"
        }
        "clip_focus" {
            Assert-CountEquals -Text $Case.Output -Pattern "Clip completed: (\d+) features clipped" -Expected $InputFeatureCount -Label "clip_focus"
        }
        "difference_focus" {
            Assert-CountEquals -Text $Case.Output -Pattern "Difference completed: (\d+) features" -Expected 0 -Label "difference_focus"
        }
        "intersect_focus" {
            Assert-CountEquals -Text $Case.Output -Pattern "Intersect completed: (\d+) features" -Expected $InputFeatureCount -Label "intersect_focus"
        }
        "spatial_join_focus" {
            Assert-CountEquals -Text $Case.Output -Pattern "Spatial join completed: (\d+) features" -Expected $InputFeatureCount -Label "spatial_join_focus"
            Assert-Condition -Condition (Test-Path (Join-Path $ResolvedOutputRoot "spatial_join_focus.gpkg")) -Message "spatial_join_focus output missing"
        }
        "union_focus" {
            $unionCount = Get-ReportedCount -Text $Case.Output -Pattern "Union completed: (\d+) features" -Label "union_focus"
            Assert-Condition -Condition ($unionCount -ge $InputFeatureCount) -Message ("union_focus: reported " + $unionCount + ", expected at least " + $InputFeatureCount)
        }
        "clip_far" {
            Assert-CountEquals -Text $Case.Output -Pattern "Clip completed: (\d+) features clipped" -Expected 0 -Label "clip_far"
        }
        "intersect_far" {
            Assert-CountEquals -Text $Case.Output -Pattern "Intersect completed: (\d+) features" -Expected 0 -Label "intersect_far"
        }
        "difference_far" {
            Assert-CountEquals -Text $Case.Output -Pattern "Difference completed: (\d+) features" -Expected $InputFeatureCount -Label "difference_far"
        }
        "clip_partial" {
            $partialCount = Get-ReportedCount -Text $Case.Output -Pattern "Clip completed: (\d+) features clipped" -Label "clip_partial"
            Assert-Condition -Condition ($partialCount -gt 0 -and $partialCount -le $InputFeatureCount) -Message ("clip_partial: reported " + $partialCount + ", expected in (0, " + $InputFeatureCount + "]")
            $script:ClipPartialCount = $partialCount
        }
        "intersect_partial" {
            $partialCount = Get-ReportedCount -Text $Case.Output -Pattern "Intersect completed: (\d+) features" -Label "intersect_partial"
            Assert-Condition -Condition ($partialCount -gt 0 -and $partialCount -le $InputFeatureCount) -Message ("intersect_partial: reported " + $partialCount + ", expected in (0, " + $InputFeatureCount + "]")
            Assert-Condition -Condition ($null -ne $script:ClipPartialCount) -Message "intersect_partial ran before clip_partial, cannot cross-check"
            Assert-Condition -Condition ($script:ClipPartialCount -eq $partialCount) -Message ("clip and intersect disagree on the same partial window: " + $script:ClipPartialCount + " vs " + $partialCount)
        }
        "dissolve_bbox" {
            Assert-TextContains -Text $Case.Output -Expected "Dissolve completed: 1 features" -Message "dissolve_bbox feature count mismatch"
        }
        "convert_roads_geojson" {
            # 不用 ConvertFrom-Json 整读：PS 5.1 的 Get-Content 默认按 ANSI 代码页解码，
            # 这份 12 MB / 22836 要素的 UTF-8 GeoJSON 里中文属性值会被读坏，
            # 报的是"JSON 语法无效 (position 230)"这种假产品缺陷。
            # 改判两件事：文件头是 FeatureCollection、且转出的图层能被读回同样的要素数。
            $geojsonPath = Join-Path $ResolvedOutputRoot "roads_core.geojson"
            $head = (@(Get-Content -Path $geojsonPath -TotalCount 3 -Encoding UTF8) -join " ")
            Assert-Condition -Condition ($head.Contains('"FeatureCollection"')) -Message ("convert_roads_geojson: header is not a FeatureCollection, got: " + $head)
            $converted = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $geojsonPath -ScratchDir $ResolvedOutputRoot
            Assert-Condition -Condition ($converted.FeatureCount -eq $InputFeatureCount) -Message ("convert_roads_geojson feature count " + $converted.FeatureCount + " != input " + $InputFeatureCount)
        }
        "rasterize_bbox" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "bbox_rasterized.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   46 x 35 x 1 bands" -Message "rasterize_bbox raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "rasterize_bbox CRS mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   1" -Message "rasterize_bbox mean mismatch"
        }
        "polygonize_bbox_raster" {
            Assert-TextContains -Text $Case.Output -Expected "Polygonize completed: 1 polygons" -Message "polygonize_bbox_raster polygon count mismatch"
        }
        "clip_stress" {
            Assert-CountEquals -Text $Case.Output -Pattern "Clip completed: (\d+) features clipped" -Expected $InputFeatureCount -Label "clip_stress"
        }
        "difference_stress" {
            Assert-CountEquals -Text $Case.Output -Pattern "Difference completed: (\d+) features" -Expected 0 -Label "difference_stress"
        }
    }
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

# 三个窗口按输入图层自己的范围生成，因此对"合成样本"和"data/vector 真实道路"都成立：
#   all     —— 外扩 20%，必须完全覆盖输入 ⇒ clip/intersect/ spatial_join 是恒等、difference 是空；
#   far     —— 整块挪出范围之外 ⇒ 三个叠加动作必须一个都不留 / 一个都不少；
#   partial —— 取范围中间的 25%~75% ⇒ 真正会切到几何，用来钉住 clip 与 intersect 的一致性。
$roadsStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $roadsCore -ScratchDir $ResolvedOutputRoot
$InputFeatureCount = $roadsStats.FeatureCount
$spanX = [Math]::Max($roadsStats.MaxX - $roadsStats.MinX, 0.001)
$spanY = [Math]::Max($roadsStats.MaxY - $roadsStats.MinY, 0.001)

$allBox = @(
    ($roadsStats.MinX - 0.2 * $spanX),
    ($roadsStats.MinY - 0.2 * $spanY),
    ($roadsStats.MaxX + 0.2 * $spanX),
    ($roadsStats.MaxY + 0.2 * $spanY)
)
if (($roadsStats.MinY - 3 * $spanY) -gt -89) {
    $farBox = @($roadsStats.MinX, ($roadsStats.MinY - 3 * $spanY), $roadsStats.MaxX, ($roadsStats.MinY - 2 * $spanY))
} else {
    $farBox = @($roadsStats.MinX, ($roadsStats.MaxY + 2 * $spanY), $roadsStats.MaxX, ($roadsStats.MaxY + 3 * $spanY))
}
$partialBox = @(
    ($roadsStats.MinX + 0.25 * $spanX),
    ($roadsStats.MinY + 0.25 * $spanY),
    ($roadsStats.MinX + 0.75 * $spanX),
    ($roadsStats.MinY + 0.75 * $spanY)
)

$allWindow = Join-Path $benchmarkRoot "win_all.geojson"
$farWindow = Join-Path $benchmarkRoot "win_far.geojson"
$partialWindow = Join-Path $benchmarkRoot "win_partial.geojson"
Write-BboxGeojson -Path $allWindow -Box $allBox -Label "win_all"
Write-BboxGeojson -Path $farWindow -Box $farBox -Label "win_far"
Write-BboxGeojson -Path $partialWindow -Box $partialBox -Label "win_partial"

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
    ("--clip_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "clip_focus.gpkg")
)
$cases += New-Case -Name "difference_focus" -CaseArgs @(
    "vector", "difference",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "difference_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "difference_focus.gpkg")
)
$cases += New-Case -Name "union_focus" -CaseArgs @(
    "vector", "union",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "union_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "union_focus.gpkg")
)
$cases += New-Case -Name "intersect_focus" -CaseArgs @(
    "vector", "intersect",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "intersect_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "intersect_focus.gpkg")
)
$cases += New-Case -Name "clip_far" -CaseArgs @(
    "vector", "clip",
    ("--input=" + $roadsCore),
    ("--clip_vector=" + $farWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_far.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "clip_far.gpkg")
)
$cases += New-Case -Name "intersect_far" -CaseArgs @(
    "vector", "intersect",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $farWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "intersect_far.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "intersect_far.gpkg")
)
$cases += New-Case -Name "difference_far" -CaseArgs @(
    "vector", "difference",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $farWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "difference_far.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "difference_far.gpkg")
)
$cases += New-Case -Name "clip_partial" -CaseArgs @(
    "vector", "clip",
    ("--input=" + $roadsCore),
    ("--clip_vector=" + $partialWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_partial.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "clip_partial.gpkg")
)
$cases += New-Case -Name "intersect_partial" -CaseArgs @(
    "vector", "intersect",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $partialWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "intersect_partial.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "intersect_partial.gpkg")
)
$cases += New-Case -Name "spatial_join_focus" -CaseArgs @(
    "vector", "spatial_join",
    ("--input=" + $roadsCore),
    ("--join_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "spatial_join_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "spatial_join_focus.gpkg")
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
        ("--clip_vector=" + $allWindow),
        ("--output=" + (Join-Path $ResolvedOutputRoot "clip_stress.gpkg"))
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "clip_stress.gpkg")
    )
    $cases += New-Case -Name "difference_stress" -CaseArgs @(
        "vector", "difference",
        ("--input=" + $roadsCore),
        ("--overlay_vector=" + $allWindow),
        ("--output=" + (Join-Path $ResolvedOutputRoot "difference_stress.gpkg"))
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "difference_stress.gpkg")
    )
}

$results = @()
foreach ($case in $cases) {
    $result = Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
    Validate-CaseOutputs -Case $result -ResolvedCliPath $ResolvedCliPath -ResolvedOutputRoot $ResolvedOutputRoot -InputFeatureCount $InputFeatureCount
    $results += $result
}

Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Vector regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(20) + " " + $result.Seconds + "s")
}
Write-Host ("  summary_json".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".json")))
Write-Host ("  summary_csv".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".csv")))
