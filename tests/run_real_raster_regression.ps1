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

# D1-T1: value-level reader for the per-band numbers printed by `raster_inspect info`.
# WHY a regex on the whole line instead of Assert-TextContains on "Mean:   8": a substring
# lets "89.98" satisfy an expectation of "8" (and "899" satisfy "89"), which is exactly the
# vacuous-judge shape the registry header forbids. Here the label is anchored to its own
# column and the captured token must parse as a full number, then we compare numerically.
# SCOPE (D1-T1x, reviewer R3): [regex]::Match returns the FIRST match, so on a multi-band
# product this judges band 1 only and never says so. Reachable gap today -- every block that
# uses it reads a single-band product (measured "Size: 48 x 48 x 1 bands") -- but the moment a
# multi-band case is added, either loop the label per band or make this function fail closed.
# Same root as the terrain "-b" lever being undecidable on the fixture pool (see the ledger in
# tests/test_plugins.cpp above the terrain HandValue cases; left for #164 / multi-band fixtures).
function Get-InfoStatValue {
    param(
        [string]$Text,
        [string]$Label
    )

    $pattern = "(?m)^\s*" + $Label + ":\s*(-?[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)\s*$"
    $match = [regex]::Match($Text, $pattern)
    Assert-Condition -Condition ($match.Success) -Message ("raster_inspect info has no numeric '" + $Label + "' line")
    return [double]$match.Groups[1].Value
}

function Assert-InfoStatNear {
    param(
        [string]$Text,
        [string]$Label,
        [double]$Expected,
        [double]$Tolerance,
        [string]$Message
    )

    $actual = Get-InfoStatValue -Text $Text -Label $Label
    Assert-Condition -Condition ([math]::Abs($actual - $Expected) -le $Tolerance) -Message (
        $Message + " (actual=" + $actual + " expected=" + $Expected + " tolerance=" + $Tolerance + ")"
    )
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
        # ---- D1-T1: terrain value-level judgements (batch D first promotion knife) ----
        # Input for all six blocks below is the generated terrain DEM:
        #   48 x 48 Float32, z(x,y) = x*1.5 + y*0.8 + ((x*y)%11)*0.2   (gui_test_data_helper.cpp:595-624)
        #   cell size 0.0005 x 0.0005, no CRS, no source NODATA          (gui_test_data_helper.cpp:612-615)
        # Every expected number below is produced by an independent reference implementation of the
        # documented formulas (Horn first differences + GDAL slope/aspect/hillshade, Riley TIN TRI,
        # center-minus-8-neighbour-mean TPI, cv::Laplacian ksize=3 = d2/dx2 + d2/dy2), not read back
        # from the product. Reference and product agreed to float32 precision at the probe stage.
        # The tolerance (1e-3 .. 5e-3) only absorbs float32/double tail; any algorithm-constant change
        # moves these numbers by >= 0.5, which is why they are load-bearing.
        "terrain_slope" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_slope_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   48 x 48 x 1 bands" -Message "terrain_slope raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_slope raster type mismatch"
            # Horn slope = atan(sqrt(dzdx^2+dzdy^2)) in degrees. dzdx/dzdy here are ~1e3 because the
            # DEM climbs 1.5 and 0.8 z-units per 0.0005 ground unit, so every interior pixel lands in
            # (89.96, 89.99). Statistics exclude the 1-pixel Horn border, which GDAL fills with its
            # default NODATA -9999; pinning that value is what proves the border is still NODATA and
            # did not silently become 0 or 45.
            Assert-InfoStatNear -Text $info -Label "NoData" -Expected -9999.0 -Tolerance 1e-6 -Message "terrain_slope nodata sentinel mismatch"
            Assert-InfoStatNear -Text $info -Label "Min" -Expected 89.968927 -Tolerance 1e-3 -Message "terrain_slope min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 89.988577 -Tolerance 1e-3 -Message "terrain_slope max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 89.982932 -Tolerance 1e-3 -Message "terrain_slope mean mismatch"
        }
        "terrain_aspect" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_aspect_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_aspect raster type mismatch"
            # aspect = 90 - atan2(dzdy, -dzdx) in degrees, wrapped into [0,360). With dzdx ~ +3000 and
            # dzdy ~ +1600 the plane falls to the west-north-west, so the interior band is 270..329:
            # min is exactly the "pure east rise, no noise" limit 270 and mean 298.176. A sign flip in
            # either partial derivative maps 270->90 and moves the mean by ~90 deg, so this is a
            # directional pin, not a range check.
            Assert-InfoStatNear -Text $info -Label "NoData" -Expected -9999.0 -Tolerance 1e-6 -Message "terrain_aspect nodata sentinel mismatch"
            Assert-InfoStatNear -Text $info -Label "Min" -Expected 269.999958 -Tolerance 1e-3 -Message "terrain_aspect min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 328.448646 -Tolerance 1e-3 -Message "terrain_aspect max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 298.176267 -Tolerance 1e-3 -Message "terrain_aspect mean mismatch"
        }
        "terrain_hillshade" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_hillshade_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Type:   Byte" -Message "terrain_hillshade raster type mismatch"
            # ESRI model: hs = 255*(cos(zen)cos(slope) + sin(zen)sin(slope)cos(az-aspect)),
            # zen = 90-altitude. D1-T1x moved the parameter point from az=315/alt=45 to az=330/alt=35:
            # 315 is the reflection axis of the Horn x<->y kernels (swapping them leaves cos(az-aspect)
            # unchanged) and alt=45 is the fixed point of zen = 90-altitude (zen == alt), so at the old
            # point two whole mutation classes were invisible no matter how tight the band was -- the
            # reviewer measured |delta mean| = 0.0000 for both and my own recomputation agrees.
            # The band was NOT widened to make this work: tolerance is still +-0.55 (a Byte band's
            # quantisation uncertainty is inherently 1.0 wide, so +-0.55 around the unquantised
            # reference mean is already close to the narrowest usable band).
            # Reference (independent numpy implementation of the formula above over the float32 DEM
            # z = 1.5x + 0.8y + ((x*y)%11)*0.2, cell 0.0005, the 2116 non-NODATA interior pixels):
            #   raw   min/max/mean = 104.497867 / 208.850938 / 174.580886
            #   ceil-law mean      = 175.057656   floor-law mean = 174.057656
            # Pinned Mean is the *unquantised* reference 174.580886, NOT the product's printed value
            # 174.865 (raster_inspect prints 6 significant digits, raster_inspect_plugin.cpp:282-285;
            # a copy-back would have pinned 174.865). Measured margin: |174.865 - 174.580886| =
            # 0.284114 inside +-0.55 -> 0.266 left (the old point left 0.214).
            # Sensitivity at the new point (|delta mean| from the same reference implementation):
            #   swap dzdx <-> dzdy kernels        30.757777   (was 0.0000 at az=315)
            #   zen = altitude instead of 90-alt  52.306318   (was 0.0000 at alt=45)
            #   drop -az (GDAL default azimuth)   22.078407
            #   drop -alt (GDAL default altitude)  9.981752
            #   aspect written as 360-aspect     181.0442     cos(az-aspect) replaced by 1: 34.3464
            #   z_factor 0.5 / 10                  0.043541 / 0.039203  <- NOT catchable on this DEM:
            #     it climbs 1.5/0.8 z-units per 0.0005 ground unit, so every interior slope is ~89.97
            #     deg, atan saturates and scaling both partials leaves the aspect ratio untouched.
            #     That lever is pinned on the analytic plane in the fixture case instead
            #     (DEM-P z=10 -> 198 vs z=1 -> 255); the slope-side silence is filed as N163.
            # Min/Max carry +-1 for the same rounding reason (measured product: Min 105, Max 209).
            Assert-InfoStatNear -Text $info -Label "Min" -Expected 105.0 -Tolerance 1.0 -Message "terrain_hillshade min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 209.0 -Tolerance 1.0 -Message "terrain_hillshade max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 174.580886 -Tolerance 0.55 -Message "terrain_hillshade mean mismatch"
        }
        "terrain_tpi" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_tpi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_tpi raster type mismatch"
            # TPI = centre - mean(8 neighbours) with BORDER_REPLICATE over all 2304 pixels (no NODATA on
            # the local path). The signed extremes come from the border rows/columns (replication counts
            # the centre column twice) and the noise term: -1.237502 .. +1.637502, mean 0. If the
            # centre were left inside the neighbourhood mean (sum9/9) the whole field would shift by
            # roughly centre/9 - centre/8 ~ +-6 and the mean would no longer be 0.
            Assert-InfoStatNear -Text $info -Label "Min" -Expected -1.237502 -Tolerance 2e-3 -Message "terrain_tpi min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 1.637502 -Tolerance 2e-3 -Message "terrain_tpi max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 0.0 -Tolerance 1e-3 -Message "terrain_tpi mean mismatch"
        }
        "terrain_curvature" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_curvature_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   48 x 48 x 1 bands" -Message "terrain_curvature raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_curvature raster type mismatch"
            # cv::Laplacian(ksize=3) = d2/dx2 + d2/dy2 with the Sobel-smoothed second differences, i.e.
            # K = [[2,0,2],[0,-8,0],[2,0,2]] (verified pixel by pixel against a single-pulse impulse).
            # On this DEM the linear part cancels and only the ((x*y)%11)*0.2 term survives, plus the
            # replicated border: extremes -16.000015 / +13.200020, mean 0. Swapping ksize 3 -> 1 (the
            # 4-neighbour kernel) changes both extremes, so this is not a vacuous pair.
            # NOTE: the registry row terrain,curvature stays PENDING in this batch by controller
            # ruling - this block only closes the "no leg case at all" gap the brief listed.
            Assert-InfoStatNear -Text $info -Label "Min" -Expected -16.000015 -Tolerance 2e-3 -Message "terrain_curvature min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 13.200020 -Tolerance 2e-3 -Message "terrain_curvature max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 0.0 -Tolerance 5e-3 -Message "terrain_curvature mean mismatch"
        }
        "terrain_tri" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_tri_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_tri raster type mismatch"
            # Riley TIN-TRI = sqrt(sum((neighbour-centre)^2)/8) over the 8 neighbours (clamped border).
            # Reference over the full 2304 pixels: 0.810863 .. 2.303802, mean 1.718467. Dividing by 9
            # instead of 8 scales every value by sqrt(8/9) = 0.943 (mean -> 1.6206), which is 20x the
            # tolerance, so the pin is arithmetic-shape sensitive, not just range sensitive.
            Assert-InfoStatNear -Text $info -Label "Min" -Expected 0.810863 -Tolerance 2e-3 -Message "terrain_tri min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 2.303802 -Tolerance 2e-3 -Message "terrain_tri max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 1.718467 -Tolerance 2e-3 -Message "terrain_tri mean mismatch"
        }
        # ---- D1-T2: curvature family / roughness raised from type-level to value level ----
        # Same generated DEM as the six blocks above (gui_test_data_helper.cpp:595-624). These three
        # actions are this repo's own code, not GDAL: terrain_plugin.cpp:222-250 (profile/plan
        # curvature from five cv::Sobel derivatives) and :274-279 (roughness = dilate(3x3) - erode(3x3),
        # both BORDER_REPLICATE). Every expected number below comes from an independent
        # re-implementation of those formulas (build/tmp/batchD/d1t2/ref_model.py), never read back
        # from the product. Product vs model on the identical 48x48 array (probe3.py, float32 tail):
        #   profile_curvature  product -2.385006 / 3.092854 / 0.0086002   model -2.385005 / 3.092848 / 0.0086002
        #   plan_curvature     product -9.183311 / 12.066192 / 0.0190742  model -9.183276 / 12.066154 / 0.0190742
        #   roughness          product  2.500000 /  6.600006 / 4.7921877  model  2.500000 /  6.600006 / 4.7921875
        # Tolerance 2e-3 = 22x the largest observed product/model drift (9.2e-5), and every mutation
        # arm run for this knife moved at least one of these three numbers by >0.02 (readings and the
        # per-arm deltas are in task-13b-report.md section 6, recorded after the arms were run).
        # raster_inspect prints stats with the ostream default of 6 significant digits, so at magnitude
        # ~10 the last printed digit is 1e-5 -- comfortably inside the 2e-3 band, not a hidden widening.
        # UNITS: the local path never reads the geotransform (gdalBandToMat, kernels in pixel space),
        # so these are per-pixel derivatives. The very same 48x48 array with cell size 0.001 (the C++
        # fixture DEM-N) produced a pixel-for-pixel identical product -- which is exactly why these
        # pins must NOT be read as a georeferencing or ground-unit-curvature claim.
        "terrain_profile_curvature" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_profile_curvature_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   48 x 48 x 1 bands" -Message "terrain_profile_curvature raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_profile_curvature raster type mismatch"
            # Interior of the 3x3 border only: this action reads the whole array (no Horn border, no
            # source NODATA on this DEM), so Min/Max sit on the replicated left/right columns where
            # zxx reaches +-7.6 while zx drops to half a step.
            Assert-InfoStatNear -Text $info -Label "Min" -Expected -2.385005 -Tolerance 2e-3 -Message "terrain_profile_curvature min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 3.092848 -Tolerance 2e-3 -Message "terrain_profile_curvature max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 0.0086002 -Tolerance 2e-3 -Message "terrain_profile_curvature mean mismatch"
        }
        "terrain_plan_curvature" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_plan_curvature_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   48 x 48 x 1 bands" -Message "terrain_plan_curvature raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_plan_curvature raster type mismatch"
            # plan and profile are NOT the same shape on this DEM: plan divides by sq^1.5 (no (1+sq)^1.5
            # damping), so its spread is ~4x wider (-9.18..12.07 vs -2.39..3.09). Swapping the two
            # formulas therefore reddens both blocks; the fixture case pins them per pixel as well.
            Assert-InfoStatNear -Text $info -Label "Min" -Expected -9.183276 -Tolerance 2e-3 -Message "terrain_plan_curvature min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 12.066154 -Tolerance 2e-3 -Message "terrain_plan_curvature max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 0.0190742 -Tolerance 2e-3 -Message "terrain_plan_curvature mean mismatch"
        }
        "terrain_roughness" {
            # This case existed since batch C but had NO judgement block at all (only the produced-file
            # existence check), so before D1-T2 the leg asserted nothing about roughness values.
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_roughness_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   48 x 48 x 1 bands" -Message "terrain_roughness raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "terrain_roughness raster type mismatch"
            # roughness = max-min of the 3x3 window (centre included, replicate border). The DEM climbs
            # 1.5 per column and 0.8 per row with a +-2.0 noise term, so the range is 2.5..6.6 and never
            # 0; tri on the same DEM is 0.81..2.30 (T1 S5), so the two blocks are not interchangeable.
            Assert-InfoStatNear -Text $info -Label "Min" -Expected 2.5 -Tolerance 2e-3 -Message "terrain_roughness min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 6.600006 -Tolerance 2e-3 -Message "terrain_roughness max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 4.7921875 -Tolerance 2e-3 -Message "terrain_roughness mean mismatch"
        }
        # ---- D1-T2: profile_extract raised to per-row value level ----
        # The product is a CSV (header distance,x,y,elevation), not a raster, so the statistical
        # template does not apply. Expected rows come from an independent sampler of
        # terrain_plugin.cpp:835-890 (steps = ceil(len/pixelStep), pixelStep = min of the two axes,
        # pixel index = lround of the affine coordinate, out-of-bounds samples dropped not clamped).
        # Path = (116.001,39.999) -> (116.010,39.992) -> (116.020,39.985) on cell size 0.0005, i.e.
        # affine (2,2) -> (20,16) -> (40,30); len1 = 0.0114018 (steps 23), len2 = 0.0122066 (steps 25),
        # first segment 24 rows + second 25 rows (its k=0 is skipped at :860) = 49 rows, all in bounds.
        # Elevation column is DEM z(x,y) = x*1.5 + y*0.8 + ((x*y)%11)*0.2 at the sampled cell:
        #   (2,2)  = 3.0+1.6+0.8  = 5.4        (20,16) = 30+12.8+0.2 = 43.0
        #   (21,17)= 31.5+13.6+1.0 = 46.1      (40,30) = 60+24.0+0.2 = 84.2
        # Row 24 is the cumulative-distance nail: without the running offset at :867 it reads
        # 0.000488 instead of 0.01189. Distances are printed with 6 significant digits, hence 1e-6.
        "terrain_profile_extract" {
            $rows = Import-Csv (Join-Path $ResolvedOutputRoot "terrain_profile_output.csv")
            Assert-Condition -Condition ($rows.Count -ge 10) -Message "terrain_profile_extract should output at least 10 rows"
            Assert-Condition -Condition ([double]$rows[0].distance -eq 0.0) -Message "terrain_profile_extract first distance should be zero"
            Assert-Condition -Condition ($rows.Count -eq 49) -Message "terrain_profile_extract row count should be 49 (steps 23+25, actual=$($rows.Count))"
            Assert-Condition -Condition ([Math]::Abs([double]$rows[0].elevation - 5.4) -lt 1e-3) -Message "terrain_profile_extract first elevation should be 5.4 (cell 2,2)"
            Assert-Condition -Condition ([Math]::Abs([double]$rows[23].distance - 0.0114018) -lt 1e-6) -Message "terrain_profile_extract last segment-1 distance should be len1=0.0114018"
            Assert-Condition -Condition ([Math]::Abs([double]$rows[23].elevation - 43.0) -lt 1e-3) -Message "terrain_profile_extract segment-1 end elevation should be 43.0 (cell 20,16)"
            Assert-Condition -Condition ([Math]::Abs([double]$rows[24].distance - 0.01189) -lt 1e-6) -Message "terrain_profile_extract segment-2 first distance should be len1+len2/25=0.01189 (cumulative)"
            Assert-Condition -Condition ([Math]::Abs([double]$rows[24].elevation - 46.1) -lt 1e-3) -Message "terrain_profile_extract segment-2 first elevation should be 46.1 (cell 21,17)"
            Assert-Condition -Condition ([Math]::Abs([double]$rows[48].distance - 0.0236083) -lt 1e-6) -Message "terrain_profile_extract last distance should be len1+len2=0.0236083"
            Assert-Condition -Condition ([Math]::Abs([double]$rows[48].elevation - 84.2) -lt 1e-3) -Message "terrain_profile_extract last elevation should be 84.2 (cell 40,30)"
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
        "processing_mean_shift_filter" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "mean_shift_filter_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   32 x 32 x 1 bands" -Message "processing_mean_shift_filter raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_mean_shift_filter raster type mismatch"
        }
        "processing_skeleton" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "skeleton_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   64 x 64 x 1 bands" -Message "processing_skeleton raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_skeleton raster type mismatch"
            Assert-TextContains -Text $info -Expected "Max:    1" -Message "processing_skeleton max mismatch"
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
        "georef_percentile_stretch" {
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_percentile_stretch_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   8 x 8 x 3 bands" -Message "georef_percentile_stretch raster size mismatch"
            Assert-TextContains -Text $info -Expected "Band 1:" -Message "georef_percentile_stretch band 1 missing"
            Assert-TextContains -Text $info -Expected "Band 2:" -Message "georef_percentile_stretch band 2 missing"
            Assert-TextContains -Text $info -Expected "Band 3:" -Message "georef_percentile_stretch band 3 missing"
            Assert-TextContains -Text $info -Expected "Mean:   0" -Message "georef_percentile_stretch mean mismatch"
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
    "--azimuth=330",
    "--altitude=35"
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

# D1-T1: terrain curvature had no leg case at all (the brief lists it as the missing key);
# added here with a value-level judgement block below. Registry row stays PENDING this batch.
$cases += New-Case -Name "terrain_curvature" -CaseArgs @(
    "terrain", "curvature",
    ("--input=" + $data.TerrainRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "terrain_curvature_output.tif")),
    "--band=1",
    "--z_factor=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "terrain_curvature_output.tif")
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
    "--pan_method=mean_blend"
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

$cases += New-Case -Name "processing_mean_shift_filter" -CaseArgs @(
    "processing", "mean_shift_filter",
    ("--input=" + $data.AnalysisRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "mean_shift_filter_output.tif")),
    "--band=1",
    "--spatial_radius=8",
    "--color_radius=16",
    "--pyramid_level=1"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "mean_shift_filter_output.tif")
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

$cases += New-Case -Name "georef_percentile_stretch" -CaseArgs @(
    "georef", "percentile_stretch",
    ("--input=" + $data.GeorefQuacRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "georef_percentile_stretch_output.tif")),
    "--dark_percentile=1",
    "--bright_percentile=99"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "georef_percentile_stretch_output.tif")
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
