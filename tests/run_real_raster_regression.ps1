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
    $debugCli = Join-Path $Root "build\debug\src\cli\Debug\gis-cli.exe"
    if (Test-Path $debugCli) {
        return $debugCli
    }
    return Join-Path $Root "build\release\src\cli\Release\gis-cli.exe"
}

function Get-DefaultHelperPath {
    param([string]$Root)
    $debugHelper = Join-Path $Root "build\debug\tests\Debug\gui_test_data_helper.exe"
    if (Test-Path $debugHelper) {
        return $debugHelper
    }
    return Join-Path $Root "build\release\tests\Release\gui_test_data_helper.exe"
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
    $combinedOutput = ($stdout + [Environment]::NewLine + $stderr).Trim()
    return [pscustomobject]@{
        Name = $Case.Name
        ExitCode = $code
        Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
        Output = $combinedOutput
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

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Read-JsonPayload {
    param([string]$Path)

    $raw = Get-Content $Path -Raw
    $jsonStart = $raw.IndexOf("{")
    Assert-Condition -Condition ($jsonStart -ge 0) -Message ("JSON payload missing: " + $Path)
    return ($raw.Substring($jsonStart) | ConvertFrom-Json)
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

function Validate-CaseOutputs {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedOutputRoot,
        [string]$ResolvedCliPath
    )

    switch ($Case.Name) {
        "spindex_ndvi" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "ndvi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 24 x 1 bands" -Message "spindex_ndvi raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   0.75" -Message "spindex_ndvi mean mismatch"
        }
        "spindex_ndmi" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "ndmi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Mean:   -0.125" -Message "spindex_ndmi mean mismatch"
        }
        "spindex_bsi" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "bsi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Mean:   0" -Message "spindex_bsi mean mismatch"
        }
        "spindex_evi2" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "evi2_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Mean:   1.57895" -Message "spindex_evi2 mean mismatch"
        }
        "raster_inspect_histogram" {
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "histogram_output.json")
            Assert-Condition -Condition ($payload.total -gt 0) -Message "histogram total should be greater than zero"
            Assert-Condition -Condition ($payload.histogram.Count -eq 16) -Message "histogram bin count should equal 16"
        }
        "terrain_slope" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_slope_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   48 x 48 x 1 bands" -Message "terrain_slope raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_slope raster type mismatch"
        }
        "terrain_tri" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_tri_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_tri raster type mismatch"
        }
        "terrain_profile_curvature" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_profile_curvature_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_profile_curvature raster type mismatch"
        }
        "terrain_plan_curvature" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_plan_curvature_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_plan_curvature raster type mismatch"
        }
        "terrain_profile_extract" {
            $rows = Import-Csv (Join-Path $ResolvedOutputRoot "terrain_profile_output.csv")
            Assert-Condition -Condition ($rows.Count -ge 10) -Message "terrain_profile_extract should output at least 10 rows"
            Assert-Condition -Condition ([double]$rows[0].distance -eq 0.0) -Message "terrain_profile_extract first distance should be zero"
        }
        "terrain_viewshed_multi" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_viewshed_multi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Min:    0" -Message "terrain_viewshed_multi min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    255" -Message "terrain_viewshed_multi max mismatch"
        }
        "projection_info" {
            $text = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "projection", "info", ("--input=" + $data.ClassificationRaster)
            )
            Assert-TextContains -Text $text -Expected "Size: 6 x 6 x 1 bands" -Message "projection_info size mismatch"
            Assert-TextContains -Text $text -Expected "Authority: EPSG:3857" -Message "projection_info authority mismatch"
        }
        "projection_transform" {
            $text = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "projection", "transform",
                "--src_srs=EPSG:4326",
                "--dst_srs=EPSG:3857",
                "--x=116",
                "--y=40"
            )
            Assert-TextContains -Text $text -Expected "(116, 40) ->" -Message "projection_transform output format mismatch"
            Assert-TextContains -Text $text -Expected "12913060.93" -Message "projection_transform dst_x mismatch"
            Assert-TextContains -Text $text -Expected "4865942.28" -Message "projection_transform dst_y mismatch"
        }
        "projection_assign_srs" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "projection_assign_input.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 24 x 6 bands" -Message "projection_assign_srs raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "projection_assign_srs crs mismatch"
        }
        "projection_reproject" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "projection_reproject_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   7 x 5 x 1 bands" -Message "projection_reproject raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "projection_reproject crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   1" -Message "projection_reproject mean mismatch"
        }
        "cutting_clip" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "cutting_clip_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   2 x 2 x 1 bands" -Message "cutting_clip raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:3857" -Message "cutting_clip crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   1" -Message "cutting_clip mean mismatch"
        }
        "cutting_mosaic" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "cutting_mosaic_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   6 x 6 x 1 bands" -Message "cutting_mosaic raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:3857" -Message "cutting_mosaic crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   1" -Message "cutting_mosaic mean mismatch"
        }
        "cutting_split" {
            $tilesDir = Join-Path $ResolvedOutputRoot "cutting_split_tiles"
            $tiles = @(Get-ChildItem $tilesDir -Filter *.tif -ErrorAction Stop)
            Assert-Condition -Condition ($tiles.Count -eq 4) -Message "cutting_split tile count mismatch"
            $firstTile = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $tiles[0].FullName)
            )
            Assert-TextContains -Text $firstTile -Expected "Size:   4 x 4 x 1 bands" -Message "cutting_split first tile size mismatch"
        }
        "cutting_merge_bands" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "cutting_merge_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 24 x 12 bands" -Message "cutting_merge_bands raster size mismatch"
            Assert-TextContains -Text $info -Expected "Band 12:" -Message "cutting_merge_bands band count mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   110" -Message "cutting_merge_bands final band mean mismatch"
        }
        "processing_pansharpen" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "pansharpen_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   30 x 30 x 3 bands" -Message "processing_pansharpen raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   75" -Message "processing_pansharpen band1 mean mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   85" -Message "processing_pansharpen band2 mean mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   95" -Message "processing_pansharpen band3 mean mismatch"
        }
        "processing_gabor_filter" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "gabor_filter_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   32 x 32 x 1 bands" -Message "processing_gabor_filter raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_gabor_filter raster type mismatch"
        }
        "processing_glcm_texture" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "glcm_texture_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   32 x 32 x 1 bands" -Message "processing_glcm_texture raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_glcm_texture raster type mismatch"
        }
        "processing_mean_shift_segment" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "mean_shift_segment_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   32 x 32 x 1 bands" -Message "processing_mean_shift_segment raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_mean_shift_segment raster type mismatch"
        }
        "processing_skeleton" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "skeleton_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   64 x 64 x 1 bands" -Message "processing_skeleton raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_skeleton raster type mismatch"
            Assert-TextContains -Text $info -Expected "Max:    255" -Message "processing_skeleton max mismatch"
        }
        "processing_connected_components" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "connected_components_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   64 x 64 x 1 bands" -Message "processing_connected_components raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_connected_components raster type mismatch"
            Assert-TextContains -Text $info -Expected "Max:    4" -Message "processing_connected_components component count mismatch"
        }
        "georef_dos_correction" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_dos_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   16 x 16 x 1 bands" -Message "georef_dos_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "georef_dos_correction raster type mismatch"
            Assert-TextContains -Text $info -Expected "Min:    0" -Message "georef_dos_correction min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    1" -Message "georef_dos_correction max mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   0.5" -Message "georef_dos_correction mean mismatch"
        }
        "georef_radiometric_calibration" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_radiometric_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   16 x 16 x 1 bands" -Message "georef_radiometric_calibration raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "georef_radiometric_calibration raster type mismatch"
            Assert-TextContains -Text $info -Expected "Min:    2" -Message "georef_radiometric_calibration min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    767" -Message "georef_radiometric_calibration max mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   384.5" -Message "georef_radiometric_calibration mean mismatch"
        }
        "georef_gcp_register" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_gcp_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_gcp_register raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "georef_gcp_register crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   7" -Message "georef_gcp_register mean mismatch"
        }
        "georef_cosine_correction" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_cosine_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_cosine_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   20" -Message "georef_cosine_correction mean mismatch"
        }
        "georef_minnaert_correction" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_minnaert_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_minnaert_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   14.1421" -Message "georef_minnaert_correction mean mismatch"
        }
        "georef_c_correction" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_c_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_c_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   18.1239" -Message "georef_c_correction mean mismatch"
        }
        "georef_quac_correction" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_quac_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   8 x 8 x 3 bands" -Message "georef_quac_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Band 1:" -Message "georef_quac_correction band 1 missing"
            Assert-TextContains -Text $info -Expected "Band 2:" -Message "georef_quac_correction band 2 missing"
            Assert-TextContains -Text $info -Expected "Band 3:" -Message "georef_quac_correction band 3 missing"
            Assert-TextContains -Text $info -Expected "Mean:   0" -Message "georef_quac_correction mean mismatch"
        }
        "georef_rpc_orthorectify" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_rpc_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_rpc_orthorectify raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "georef_rpc_orthorectify crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   9" -Message "georef_rpc_orthorectify mean mismatch"
        }
        "classification_feature_stats" {
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "feature_stats_output.json")
            Assert-Condition -Condition ($payload.meta.actual_srs -eq "EPSG:3857") -Message "classification_feature_stats actual_srs mismatch"
            Assert-Condition -Condition ($payload.records.Count -ge 2) -Message "classification_feature_stats should output at least two records"
            $summary = @($payload.records | Where-Object { $_.feature_id -eq "__summary__" })
            Assert-Condition -Condition ($summary.Count -eq 1) -Message "classification_feature_stats summary record missing"
        }
        "classification_feature_stats_csv" {
            $rows = Import-Csv (Join-Path $ResolvedOutputRoot "feature_stats_output.csv")
            Assert-Condition -Condition ($rows.Count -ge 2) -Message "classification_feature_stats_csv should output at least two rows"
            Assert-Condition -Condition ($rows[0].PSObject.Properties.Name -contains "feature_id") -Message "classification_feature_stats_csv missing feature_id column"
            Assert-Condition -Condition ($rows[0].PSObject.Properties.Name -contains "pixel_count") -Message "classification_feature_stats_csv missing pixel_count column"
        }
        "classification_svm_classify" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "classification_svm_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 12 x 1 bands" -Message "classification_svm_classify raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "classification_svm_classify raster type mismatch"
            Assert-TextContains -Text $info -Expected "Min:    1" -Message "classification_svm_classify min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    2" -Message "classification_svm_classify max mismatch"
        }
        "classification_random_forest_classify" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "classification_rf_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 12 x 1 bands" -Message "classification_random_forest_classify raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "classification_random_forest_classify raster type mismatch"
            Assert-TextContains -Text $info -Expected "Min:    1" -Message "classification_random_forest_classify min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    2" -Message "classification_random_forest_classify max mismatch"
        }
        "classification_max_likelihood_classify" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "classification_ml_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 12 x 1 bands" -Message "classification_max_likelihood_classify raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "classification_max_likelihood_classify raster type mismatch"
            Assert-TextContains -Text $info -Expected "Min:    1" -Message "classification_max_likelihood_classify min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    2" -Message "classification_max_likelihood_classify max mismatch"
        }
    }
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
    $ndviInput = Join-Path $generatedRoot "ndvi_input.tif"
    $panMs = Join-Path $rasterDataRoot "pansharpen_ms.tif"
    $panPan = Join-Path $rasterDataRoot "pansharpen_pan.tif"
    $classVector = Join-Path $rasterDataRoot "classification_vector.gpkg"
    $classMap = Join-Path $rasterDataRoot "classification_class_map.json"
    $classRaster = Join-Path $rasterDataRoot "classification_raster.tif"
    $analysisRaster = Join-Path $generatedRoot "analysis_input.tif"
    $processingBinaryRaster = Join-Path $generatedRoot "processing_binary_input.tif"
    $terrainRaster = Join-Path $generatedRoot "terrain_input.tif"
    $supervisedClassificationRaster = Join-Path $generatedRoot "classification_supervised_input.tif"
    $supervisedClassificationCsv = Join-Path $generatedRoot "classification_supervised_samples.csv"
    $georefRadiometricRaster = Join-Path $generatedRoot "georef_radiometric_input.tif"
    $georefRadiometricMetadata = Join-Path $generatedRoot "georef_radiometric_metadata.txt"
    $georefGcpRaster = Join-Path $generatedRoot "georef_gcp_input.tif"
    $georefGcpCsv = Join-Path $generatedRoot "georef_gcps.csv"
    $georefTopoInput = Join-Path $generatedRoot "georef_topo_input.tif"
    $georefTopoSlope = Join-Path $generatedRoot "georef_topo_slope.tif"
    $georefTopoAspect = Join-Path $generatedRoot "georef_topo_aspect.tif"
    $georefQuacRaster = Join-Path $generatedRoot "georef_quac_input.tif"
    $georefRpcRaster = Join-Path $generatedRoot "georef_rpc_input.tif"

    if (-not (Test-Path $ndviInput)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("ndvi-raster", $ndviInput)
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

    if (-not (Test-Path $analysisRaster)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("analysis-raster", $analysisRaster)
    }

    if (-not (Test-Path $processingBinaryRaster)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("processing-binary-raster", $processingBinaryRaster)
    }

    if (-not (Test-Path $terrainRaster)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("terrain-raster", $terrainRaster)
    }

    if ((-not (Test-Path $supervisedClassificationRaster)) -or (-not (Test-Path $supervisedClassificationCsv))) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @(
            "classification-supervised-inputs",
            $supervisedClassificationRaster,
            $supervisedClassificationCsv
        )
    }

    if ((-not (Test-Path $georefRadiometricRaster)) -or (-not (Test-Path $georefRadiometricMetadata))) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @(
            "georef-radiometric-inputs",
            $georefRadiometricRaster,
            $georefRadiometricMetadata
        )
    }

    if ((-not (Test-Path $georefGcpRaster)) -or (-not (Test-Path $georefGcpCsv))) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @(
            "georef-gcp-inputs",
            $georefGcpRaster,
            $georefGcpCsv
        )
    }

    if ((-not (Test-Path $georefTopoInput)) -or (-not (Test-Path $georefTopoSlope)) -or (-not (Test-Path $georefTopoAspect))) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @(
            "georef-topographic-inputs",
            $georefTopoInput,
            $georefTopoSlope,
            $georefTopoAspect
        )
    }

    if (-not (Test-Path $georefQuacRaster)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @(
            "georef-quac-input",
            $georefQuacRaster
        )
    }

    if (-not (Test-Path $georefRpcRaster)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @(
            "georef-rpc-input",
            $georefRpcRaster
        )
    }

    return [pscustomobject]@{
        NdviInput = $ndviInput
        PansharpenMs = $panMs
        PansharpenPan = $panPan
        ClassificationVector = $classVector
        ClassificationClassMap = $classMap
        ClassificationRaster = $classRaster
        AnalysisRaster = $analysisRaster
        ProcessingBinaryRaster = $processingBinaryRaster
        TerrainRaster = $terrainRaster
        SupervisedClassificationRaster = $supervisedClassificationRaster
        SupervisedClassificationCsv = $supervisedClassificationCsv
        GeorefRadiometricRaster = $georefRadiometricRaster
        GeorefRadiometricMetadata = $georefRadiometricMetadata
        GeorefGcpRaster = $georefGcpRaster
        GeorefGcpCsv = $georefGcpCsv
        GeorefTopoInput = $georefTopoInput
        GeorefTopoSlope = $georefTopoSlope
        GeorefTopoAspect = $georefTopoAspect
        GeorefQuacRaster = $georefQuacRaster
        GeorefRpcRaster = $georefRpcRaster
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

$cases += New-Case -Name "spindex_evi2" -CaseArgs @(
    "spindex", "evi2",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "evi2_output.tif")),
    "--red_band=1",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "evi2_output.tif")
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

$cases += New-Case -Name "spindex_ndmi" -CaseArgs @(
    "spindex", "ndmi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "ndmi_output.tif")),
    "--nir_band=4",
    "--swir1_band=5"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "ndmi_output.tif")
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

$cases += New-Case -Name "spindex_bsi" -CaseArgs @(
    "spindex", "bsi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "bsi_output.tif")),
    "--blue_band=3",
    "--red_band=1",
    "--nir_band=4",
    "--swir1_band=5"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "bsi_output.tif")
)

$cases += New-Case -Name "spindex_arvi" -CaseArgs @(
    "spindex", "arvi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "arvi_output.tif")),
    "--blue_band=1",
    "--red_band=3",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "arvi_output.tif")
)

$cases += New-Case -Name "spindex_nbr" -CaseArgs @(
    "spindex", "nbr",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "nbr_output.tif")),
    "--nir_band=4",
    "--swir2_band=6"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "nbr_output.tif")
)

$cases += New-Case -Name "spindex_awei" -CaseArgs @(
    "spindex", "awei",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "awei_output.tif")),
    "--green_band=2",
    "--nir_band=4",
    "--swir1_band=5",
    "--swir2_band=6"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "awei_output.tif")
)

$cases += New-Case -Name "spindex_ui" -CaseArgs @(
    "spindex", "ui",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "ui_output.tif")),
    "--nir_band=4",
    "--swir2_band=6"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "ui_output.tif")
)

$cases += New-Case -Name "spindex_bi" -CaseArgs @(
    "spindex", "bi",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "bi_output.tif")),
    "--red_band=3",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "bi_output.tif")
)

$cases += New-Case -Name "spindex_custom_index" -CaseArgs @(
    "spindex", "custom_index",
    ("--input=" + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "custom_index_output.tif")),
    "--preset=ndvi_alias",
    "--red_band=1",
    "--nir_band=4"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "custom_index_output.tif")
)

$cases += New-Case -Name "raster_math_band_math" -CaseArgs @(
    "raster_math", "band_math",
    ("--input=" + $data.PansharpenMs),
    ("--output=" + (Join-Path $ResolvedOutputRoot "band_math_output.tif")),
    "--expression=B1+B2"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "band_math_output.tif")
)

$cases += New-Case -Name "raster_inspect_histogram" -CaseArgs @(
    "raster_inspect", "histogram",
    ("--input=" + $data.AnalysisRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "histogram_output.json")),
    "--band=1",
    "--bins=16"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "histogram_output.json")
)

$cases += New-Case -Name "raster_inspect_info" -CaseArgs @(
    "raster_inspect", "info",
    ("--input=" + $data.AnalysisRaster)
) -ExpectedOutputs @()

$rasterManageOverviewsInput = Join-Path $ResolvedOutputRoot "manage_overviews_input.tif"
Copy-Item $data.AnalysisRaster $rasterManageOverviewsInput -Force
$cases += New-Case -Name "raster_manage_overviews" -CaseArgs @(
    "raster_manage", "overviews",
    ("--input=" + $rasterManageOverviewsInput),
    "--levels=2 4",
    "--resample=nearest"
) -ExpectedOutputs @(
    $rasterManageOverviewsInput
)

$rasterManageNoDataInput = Join-Path $ResolvedOutputRoot "manage_nodata_input.tif"
Copy-Item $data.AnalysisRaster $rasterManageNoDataInput -Force
$cases += New-Case -Name "raster_manage_nodata" -CaseArgs @(
    "raster_manage", "nodata",
    ("--input=" + $rasterManageNoDataInput),
    "--band=1",
    "--nodata_value=255"
) -ExpectedOutputs @(
    $rasterManageNoDataInput
)

$cases += New-Case -Name "raster_render_colormap" -CaseArgs @(
    "raster_render", "colormap",
    ("--input=" + $data.AnalysisRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "colormap_output.tif")),
    "--band=1",
    "--cmap=jet"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "colormap_output.tif")
)

$cases += New-Case -Name "terrain_slope" -CaseArgs @(
    "terrain", "slope",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_slope_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_slope_output.tif")
)

$cases += New-Case -Name "terrain_aspect" -CaseArgs @(
    "terrain", "aspect",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_aspect_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_aspect_output.tif")
)

$cases += New-Case -Name "terrain_hillshade" -CaseArgs @(
    "terrain", "hillshade",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_hillshade_output.tif")),
    "--band=1",
    "--z_factor=1",
    "--azimuth=315",
    "--altitude=45"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_hillshade_output.tif")
)

$cases += New-Case -Name "terrain_tpi" -CaseArgs @(
    "terrain", "tpi",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_tpi_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_tpi_output.tif")
)

$cases += New-Case -Name "terrain_profile_curvature" -CaseArgs @(
    "terrain", "profile_curvature",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_profile_curvature_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_profile_curvature_output.tif")
)

$cases += New-Case -Name "terrain_plan_curvature" -CaseArgs @(
    "terrain", "plan_curvature",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_plan_curvature_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_plan_curvature_output.tif")
)

$cases += New-Case -Name "terrain_tri" -CaseArgs @(
    "terrain", "tri",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_tri_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_tri_output.tif")
)

$cases += New-Case -Name "terrain_roughness" -CaseArgs @(
    "terrain", "roughness",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_roughness_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_roughness_output.tif")
)

$cases += New-Case -Name "terrain_fill_sinks" -CaseArgs @(
    "terrain", "fill_sinks",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_fill_sinks_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_fill_sinks_output.tif")
)

$cases += New-Case -Name "terrain_flow_direction" -CaseArgs @(
    "terrain", "flow_direction",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_flow_direction_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_flow_direction_output.tif")
)

$cases += New-Case -Name "terrain_flow_accumulation" -CaseArgs @(
    "terrain", "flow_accumulation",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_flow_accumulation_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_flow_accumulation_output.tif")
)

$cases += New-Case -Name "terrain_stream_extract" -CaseArgs @(
    "terrain", "stream_extract",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_stream_extract_output.tif")),
    "--band=1",
    "--z_factor=1",
    "--accum_threshold=10"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_stream_extract_output.tif")
)

$cases += New-Case -Name "terrain_watershed" -CaseArgs @(
    "terrain", "watershed",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_watershed_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_watershed_output.tif")
)

$cases += New-Case -Name "terrain_profile_extract" -CaseArgs @(
    "terrain", "profile_extract",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_profile_output.csv")),
    "--band=1",
    "--profile_path=116.001,39.999;116.010,39.992;116.020,39.985"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_profile_output.csv")
)

$cases += New-Case -Name "terrain_viewshed" -CaseArgs @(
    "terrain", "viewshed",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_viewshed_output.tif")),
    "--band=1",
    "--observer_x=116.012",
    "--observer_y=39.988",
    "--observer_height=2.0",
    "--target_height=0.0",
    "--max_distance=0.0"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_viewshed_output.tif")
)

$cases += New-Case -Name "terrain_viewshed_multi" -CaseArgs @(
    "terrain", "viewshed_multi",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_viewshed_multi_output.tif")),
    "--band=1",
    "--observer_points=116.006,39.994;116.020,39.980",
    "--observer_height=2.0",
    "--target_height=0.0",
    "--max_distance=0.0"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_viewshed_multi_output.tif")
)

$cases += New-Case -Name "terrain_cut_fill" -CaseArgs @(
    "terrain", "cut_fill",
    ("--reference=" + $data.TerrainRaster),
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_cut_fill_output.tif")),
    "--band=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_cut_fill_output.tif")
)

$cases += New-Case -Name "terrain_reservoir_volume" -CaseArgs @(
    "terrain", "reservoir_volume",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_reservoir_output.tif")),
    "--band=1",
    "--water_level=60.0"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_reservoir_output.tif")
)

$cases += New-Case -Name "projection_info" -CaseArgs @(
    "projection", "info",
    ("--input=" + $data.ClassificationRaster)
) -ExpectedOutputs @()

$projectionAssignInput = Join-Path $ResolvedOutputRoot "projection_assign_input.tif"
Copy-Item $data.NdviInput $projectionAssignInput -Force
$cases += New-Case -Name "projection_assign_srs" -CaseArgs @(
    "projection", "assign_srs",
    ("--input=" + $projectionAssignInput),
    "--srs=EPSG:4326"
) -ExpectedOutputs @(
    $projectionAssignInput
)

$cases += New-Case -Name "projection_transform" -CaseArgs @(
    "projection", "transform",
    "--src_srs=EPSG:4326",
    "--dst_srs=EPSG:3857",
    "--x=116",
    "--y=40"
) -ExpectedOutputs @()

$cases += New-Case -Name "projection_reproject" -CaseArgs @(
    "projection", "reproject",
    ("--input=" + $data.ClassificationRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "projection_reproject_output.tif")),
    "--dst_srs=EPSG:4326",
    "--resample=nearest"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "projection_reproject_output.tif")
)

$cases += New-Case -Name "cutting_clip" -CaseArgs @(
    "cutting", "clip",
    ("--input=" + $data.ClassificationRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "cutting_clip_output.tif")),
    "--extent=12935050,4852100,12935150,4852200"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "cutting_clip_output.tif")
)

$cases += New-Case -Name "cutting_mosaic" -CaseArgs @(
    "cutting", "mosaic",
    ("--input=" + $data.ClassificationRaster + "," + $data.ClassificationRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "cutting_mosaic_output.tif"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "cutting_mosaic_output.tif")
)

$cases += New-Case -Name "cutting_split" -CaseArgs @(
    "cutting", "split",
    ("--input=" + $data.ClassificationRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "cutting_split_tiles")),
    "--tile_size=4",
    "--overlap=0"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "cutting_split_tiles")
)

$cases += New-Case -Name "cutting_merge_bands" -CaseArgs @(
    "cutting", "merge_bands",
    ("--input=" + $data.NdviInput + "," + $data.NdviInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "cutting_merge_output.tif"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "cutting_merge_output.tif")
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

$cases += New-Case -Name "processing_gabor_filter" -CaseArgs @(
    "processing", "gabor_filter",
    ("--input=" + $data.AnalysisRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "gabor_filter_output.tif")),
    "--band=1",
    "--kernel_size=9",
    "--sigma=2",
    "--gabor_theta=0",
    "--gabor_lambda=6",
    "--gabor_gamma=0.5"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "gabor_filter_output.tif")
)

$cases += New-Case -Name "processing_glcm_texture" -CaseArgs @(
    "processing", "glcm_texture",
    ("--input=" + $data.AnalysisRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "glcm_texture_output.tif")),
    "--band=1",
    "--kernel_size=5",
    "--glcm_metric=contrast",
    "--glcm_levels=8"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "glcm_texture_output.tif")
)

$cases += New-Case -Name "processing_mean_shift_segment" -CaseArgs @(
    "processing", "mean_shift_segment",
    ("--input=" + $data.AnalysisRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "mean_shift_segment_output.tif")),
    "--band=1",
    "--spatial_radius=8",
    "--color_radius=16",
    "--pyramid_level=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "mean_shift_segment_output.tif")
)

$cases += New-Case -Name "processing_skeleton" -CaseArgs @(
    "processing", "skeleton",
    ("--input=" + $data.ProcessingBinaryRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "skeleton_output.tif")),
    "--band=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "skeleton_output.tif")
)

$cases += New-Case -Name "processing_connected_components" -CaseArgs @(
    "processing", "connected_components",
    ("--input=" + $data.ProcessingBinaryRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "connected_components_output.tif")),
    "--band=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "connected_components_output.tif")
)

$cases += New-Case -Name "georef_dos_correction" -CaseArgs @(
    "georef", "dos_correction",
    ("--input=" + $data.GeorefRadiometricRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_dos_output.tif")),
    "--band=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_dos_output.tif")
)

$cases += New-Case -Name "georef_radiometric_calibration" -CaseArgs @(
    "georef", "radiometric_calibration",
    ("--input=" + $data.GeorefRadiometricRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_radiometric_output.tif")),
    ("--metadata_file=" + $data.GeorefRadiometricMetadata),
    "--band=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_radiometric_output.tif")
)

$cases += New-Case -Name "georef_gcp_register" -CaseArgs @(
    "georef", "gcp_register",
    ("--input=" + $data.GeorefGcpRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_gcp_output.tif")),
    ("--gcp_file=" + $data.GeorefGcpCsv),
    "--dst_srs=EPSG:4326",
    "--resample=nearest"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_gcp_output.tif")
)

$cases += New-Case -Name "georef_cosine_correction" -CaseArgs @(
    "georef", "cosine_correction",
    ("--input=" + $data.GeorefTopoInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_cosine_output.tif")),
    "--band=1",
    ("--slope_raster=" + $data.GeorefTopoSlope),
    ("--aspect_raster=" + $data.GeorefTopoAspect),
    "--sun_zenith_deg=30",
    "--sun_azimuth_deg=180"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_cosine_output.tif")
)

$cases += New-Case -Name "georef_minnaert_correction" -CaseArgs @(
    "georef", "minnaert_correction",
    ("--input=" + $data.GeorefTopoInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_minnaert_output.tif")),
    "--band=1",
    ("--slope_raster=" + $data.GeorefTopoSlope),
    ("--aspect_raster=" + $data.GeorefTopoAspect),
    "--sun_zenith_deg=30",
    "--sun_azimuth_deg=180",
    "--minnaert_k=0.5"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_minnaert_output.tif")
)

$cases += New-Case -Name "georef_c_correction" -CaseArgs @(
    "georef", "c_correction",
    ("--input=" + $data.GeorefTopoInput),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_c_output.tif")),
    "--band=1",
    ("--slope_raster=" + $data.GeorefTopoSlope),
    ("--aspect_raster=" + $data.GeorefTopoAspect),
    "--sun_zenith_deg=30",
    "--sun_azimuth_deg=180",
    "--c_value=0.1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_c_output.tif")
)

$cases += New-Case -Name "georef_quac_correction" -CaseArgs @(
    "georef", "quac_correction",
    ("--input=" + $data.GeorefQuacRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_quac_output.tif")),
    "--dark_percentile=1",
    "--bright_percentile=99"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_quac_output.tif")
)

$cases += New-Case -Name "georef_rpc_orthorectify" -CaseArgs @(
    "georef", "rpc_orthorectify",
    ("--input=" + $data.GeorefRpcRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_rpc_output.tif")),
    "--dst_srs=EPSG:4326",
    "--rpc_height=0",
    "--resample=nearest"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_rpc_output.tif")
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

$cases += New-Case -Name "classification_svm_classify" -CaseArgs @(
    "classification", "svm_classify",
    ("--input=" + $data.SupervisedClassificationRaster),
    ("--training_csv=" + $data.SupervisedClassificationCsv),
    ("--output=" + (Join-Path $ResolvedOutputRoot "classification_svm_output.tif")),
    "--bands=1,2"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "classification_svm_output.tif")
)

$cases += New-Case -Name "classification_random_forest_classify" -CaseArgs @(
    "classification", "random_forest_classify",
    ("--input=" + $data.SupervisedClassificationRaster),
    ("--training_csv=" + $data.SupervisedClassificationCsv),
    ("--output=" + (Join-Path $ResolvedOutputRoot "classification_rf_output.tif")),
    "--bands=1,2"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "classification_rf_output.tif")
)

$cases += New-Case -Name "classification_max_likelihood_classify" -CaseArgs @(
    "classification", "max_likelihood_classify",
    ("--input=" + $data.SupervisedClassificationRaster),
    ("--training_csv=" + $data.SupervisedClassificationCsv),
    ("--output=" + (Join-Path $ResolvedOutputRoot "classification_ml_output.tif")),
    "--bands=1,2"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "classification_ml_output.tif")
)

$results = @()
foreach ($case in $cases) {
    $results += Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
    Validate-CaseOutputs -Case $case -ResolvedOutputRoot $ResolvedOutputRoot -ResolvedCliPath $ResolvedCliPath
}

Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Raster regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(28) + " " + $result.Seconds + "s")
}
