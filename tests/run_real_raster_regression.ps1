param(
    [string]$CliPath = "",
    [string]$WorkspaceRoot = "",
    [string]$OutputRoot = "",
    [string]$DataHelperPath = "",
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
    return Join-Path $Root "build\src\cli\Debug\gis-cli.exe"
}

function Get-DefaultHelperPath {
    param([string]$Root)
    return Join-Path $Root "build\debug\tests\Debug\gui_test_data_helper.exe"
}

function Get-DefaultOutputRoot {
    param([string]$Root)
    return Join-Path $Root "tmp\raster_regression"
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
    $stdoutPath = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString() + "_gis_stdout.txt")
    $stderrPath = Join-Path $env:TEMP ([System.Guid]::NewGuid().ToString() + "_gis_stderr.txt")

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $ResolvedCliPath @([string[]]$Case.Args) 1> $stdoutPath 2> $stderrPath
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorAction

    $stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { "" }
    $stderr = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { "" }
    Remove-Item $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    if ($code -ne 0) {
        throw ($Case.Name + " failed`n" + (($stdout + [Environment]::NewLine + $stderr).Trim()))
    }

    foreach ($output in [string[]]$Case.ExpectedOutputs) {
        if (-not (Test-Path $output)) {
            throw ($Case.Name + " did not produce output: " + $output)
        }
        $item = Get-Item $output
        if ($item.PSIsContainer) {
            $childCount = @(Get-ChildItem $output -Force -ErrorAction SilentlyContinue).Count
            if ($childCount -eq 0) {
                throw ($Case.Name + " produced empty directory: " + $output)
            }
        } elseif ($item.Length -le 0) {
            throw ($Case.Name + " produced empty file: " + $output)
        }
    }

    $watch.Stop()
    return [pscustomobject]@{
        Name = $Case.Name
        ExitCode = $code
        Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
    }
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

function Invoke-Helper {
    param(
        [string]$ResolvedHelperPath,
        [string[]]$Arguments
    )

    & $ResolvedHelperPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("helper failed: " + ($Arguments -join " "))
    }
}

function Ensure-RasterRegressionData {
    param(
        [string]$ResolvedWorkspaceRoot,
        [string]$ResolvedHelperPath
    )

    $benchmarkRoot = Join-Path $ResolvedWorkspaceRoot "tmp\raster_benchmark"
    $generatedRoot = Join-Path $benchmarkRoot "generated_data"
    New-Item -ItemType Directory -Force -Path $benchmarkRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $generatedRoot | Out-Null

    $rasterDataRoot = Join-Path $ResolvedWorkspaceRoot "data\raster"
    $ndviInput = Join-Path $rasterDataRoot "ndvi_input.tif"
    $panMs = Join-Path $rasterDataRoot "pansharpen_ms.tif"
    $panPan = Join-Path $rasterDataRoot "pansharpen_pan.tif"
    $classVector = Join-Path $rasterDataRoot "classification_vector.gpkg"
    $classMap = Join-Path $rasterDataRoot "classification_class_map.json"
    $classRaster = Join-Path $rasterDataRoot "classification_raster.tif"

    if (-not (Test-Path $ndviInput)) {
        $ndviInput = Join-Path $generatedRoot "ndvi_input.tif"
        if (-not (Test-Path $ndviInput)) {
            Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("ndvi-raster", $ndviInput)
        }
    }

    if ((-not (Test-Path $panMs)) -or (-not (Test-Path $panPan))) {
        $panMs = Join-Path $generatedRoot "pansharpen_ms.tif"
        $panPan = Join-Path $generatedRoot "pansharpen_pan.tif"
        if ((-not (Test-Path $panMs)) -or (-not (Test-Path $panPan))) {
            Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("pansharpen-inputs", $panMs, $panPan)
        }
    }

    if ((-not (Test-Path $classVector)) -or (-not (Test-Path $classMap)) -or (-not (Test-Path $classRaster))) {
        $classVector = Join-Path $generatedRoot "classification_vector.gpkg"
        $classMap = Join-Path $generatedRoot "classification_class_map.json"
        $classRaster = Join-Path $generatedRoot "classification_raster.tif"
        if ((-not (Test-Path $classVector)) -or (-not (Test-Path $classMap)) -or (-not (Test-Path $classRaster))) {
            Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("classification-inputs", $classVector, $classMap, $classRaster)
        }
    }

    return [pscustomobject]@{
        NdviInput = $ndviInput
        PansharpenMs = $panMs
        PansharpenPan = $panPan
        ClassificationVector = $classVector
        ClassificationClassMap = $classMap
        ClassificationRaster = $classRaster
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

if ([string]::IsNullOrWhiteSpace($DataHelperPath)) {
    $DataHelperPath = Get-DefaultHelperPath -Root $ResolvedWorkspaceRoot
}
$ResolvedHelperPath = (Resolve-Path $DataHelperPath).Path
Require-Path -Path $ResolvedHelperPath -Label "gui_test_data_helper"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Get-DefaultOutputRoot -Root $ResolvedWorkspaceRoot
}
$ResolvedOutputRoot = $OutputRoot
New-Item -ItemType Directory -Force -Path $ResolvedOutputRoot | Out-Null

Sync-PluginDlls -ResolvedCliPath $ResolvedCliPath
$data = Ensure-RasterRegressionData -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -ResolvedHelperPath $ResolvedHelperPath

$cases = @()
$cases += New-Case -Name "spindex_ndvi" -CaseArgs @(
    "spindex", "ndvi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "ndvi_output.tif")),
    "--red_band=1",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "ndvi_output.tif")
)

$cases += New-Case -Name "spindex_evi" -CaseArgs @(
    "spindex", "evi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "evi_output.tif")),
    "--blue_band=3",
    "--red_band=1",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "evi_output.tif")
)

$cases += New-Case -Name "spindex_savi" -CaseArgs @(
    "spindex", "savi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "savi_output.tif")),
    "--red_band=1",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "savi_output.tif")
)

$cases += New-Case -Name "spindex_gndvi" -CaseArgs @(
    "spindex", "gndvi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "gndvi_output.tif")),
    "--green_band=2",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "gndvi_output.tif")
)

$cases += New-Case -Name "spindex_ndwi" -CaseArgs @(
    "spindex", "ndwi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "ndwi_output.tif")),
    "--green_band=2",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "ndwi_output.tif")
)

$cases += New-Case -Name "spindex_mndwi" -CaseArgs @(
    "spindex", "mndwi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "mndwi_output.tif")),
    "--green_band=2",
    "--swir1_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "mndwi_output.tif")
)

$cases += New-Case -Name "spindex_ndbi" -CaseArgs @(
    "spindex", "ndbi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "ndbi_output.tif")),
    "--swir1_band=4",
    "--nir_band=2"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "ndbi_output.tif")
)

$cases += New-Case -Name "processing_pansharpen" -CaseArgs @(
    "processing", "pansharpen",
    ("--input=" + $data.PansharpenMs),
    ("--output=" + (Join-Path $ResolvedOutputRoot "pansharpen_output.tif")),
    ("--pan_file=" + $data.PansharpenPan),
    "--pan_method=simple_mean"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "pansharpen_output.tif")
)

$featureStatsOutput = Join-Path $ResolvedOutputRoot "feature_stats_output.json"
$featureStatsVectorOutput = Join-Path $ResolvedOutputRoot "feature_stats_classes.gpkg"
$featureStatsRasterOutput = Join-Path $ResolvedOutputRoot "feature_stats_classes.tif"
$cases += New-Case -Name "classification_feature_stats" -CaseArgs @(
    "classification", "feature_stats",
    ("--vector=" + $data.ClassificationVector),
    "--feature_id_field=id",
    "--feature_name_field=name",
    ("--class_map=" + $data.ClassificationClassMap),
    ("--rasters=" + $data.ClassificationRaster),
    "--nodatas=0",
    ("--output=" + $featureStatsOutput),
    ("--vector_output=" + $featureStatsVectorOutput),
    ("--raster_output=" + $featureStatsRasterOutput)
) -ExpectedOutputs @(
    $featureStatsOutput,
    $featureStatsVectorOutput,
    $featureStatsRasterOutput
)

if ($Mode -eq "full") {
    $cases += New-Case -Name "classification_feature_stats_csv" -CaseArgs @(
        "classification", "feature_stats",
        ("--vector=" + $data.ClassificationVector),
        "--feature_id_field=id",
        "--feature_name_field=name",
        ("--class_map=" + $data.ClassificationClassMap),
        ("--rasters=" + $data.ClassificationRaster),
        "--nodatas=0",
        ("--output=" + (Join-Path $ResolvedOutputRoot "feature_stats_output.csv"))
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "feature_stats_output.csv")
    )
}

$results = @()
foreach ($case in $cases) {
    $results += Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
}

Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Raster regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(28) + " " + $result.Seconds + "s")
}
