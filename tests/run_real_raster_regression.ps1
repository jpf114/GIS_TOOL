param(
    [string]$CliPath = "",
    [string]$WorkspaceRoot = "",
    [string]$OutputRoot = "",
    [string]$DataHelperPath = "",
    [ValidateSet("quick", "full")]
    [string]$Mode = "quick"
)

. (Join-Path $PSScriptRoot "regression_case_gate.ps1")
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

    Register-Case -Name $Name
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

# D1-C1 (Task 18): band-aware numeric reader.
# WHY a second reader instead of reusing Get-InfoStatValue: that one takes the FIRST match of the
# label, so on a multi-band product it silently judges band 1 only -- its own SCOPE comment says so.
# The merge_bands branch has to read band 12, so the label must be looked up INSIDE the "Band N:"
# block. Fails closed when the block is missing or when the block has no numeric line for the label
# ("no statistics printed" is a product state, not a zero).
function Get-InfoBandStatValue {
    param(
        [string]$Text,
        [int]$Band,
        [string]$Label
    )

    $blockPattern = "(?ms)^\s*Band " + $Band + "\s*:.*?(?=^\s*Band \d+:|\Z)"
    $block = [regex]::Match($Text, $blockPattern)
    Assert-Condition -Condition ($block.Success) -Message ("raster_inspect info has no 'Band " + $Band + "' block")
    $statPattern = "(?m)^\s*" + $Label + ":\s*(-?[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)\s*$"
    $stat = [regex]::Match($block.Value, $statPattern)
    Assert-Condition -Condition ($stat.Success) -Message (
        "band " + $Band + " has no numeric '" + $Label + "' line" + " (block read: " + $block.Value.Trim() + ")")
    return [double]$stat.Groups[1].Value
}

function Assert-InfoBandStatNear {
    param(
        [string]$Text,
        [int]$Band,
        [string]$Label,
        [double]$Expected,
        [double]$Tolerance,
        [string]$Message
    )

    $actual = Get-InfoBandStatValue -Text $Text -Band $Band -Label $Label
    Assert-Condition -Condition ([math]::Abs($actual - $Expected) -le $Tolerance) -Message (
        $Message + " (band=" + $Band + " actual=" + $actual + " expected=" + $Expected + " tolerance=" + $Tolerance + ")"
    )
}

# D1-C1 (Task 18): value-level reader for the "Size:   W x H x B bands" line.
# WHY parse instead of Assert-TextContains on "Size:   2 x 2 x 1 bands": the substring never
# compares the numbers (it cannot tell a one-pixel window error from a correct window, and it is
# also satisfied by any longer number that starts with the same digits). Here each of the three
# fields must parse as a full integer, then they are compared one by one -- width and height are
# asserted SEPARATELY on purpose, because a square product is an x/y reflexive invariant and only
# a W != H pair can see an axis swap.
function Assert-InfoSizeExact {
    param(
        [string]$Text,
        [int]$ExpectedWidth,
        [int]$ExpectedHeight,
        [int]$ExpectedBands,
        [string]$Message
    )

    $match = [regex]::Match($Text, "(?m)^\s*Size:\s*(\d+)\s+x\s+(\d+)\s+x\s+(\d+)\s+bands\s*$")
    Assert-Condition -Condition ($match.Success) -Message ($Message + " (no parseable 'Size: W x H x B bands' line)")
    $width = [int]$match.Groups[1].Value
    $height = [int]$match.Groups[2].Value
    $bands = [int]$match.Groups[3].Value
    Assert-Condition -Condition ($width -eq $ExpectedWidth) -Message (
        $Message + " (width actual=" + $width + " expected=" + $ExpectedWidth + ")")
    Assert-Condition -Condition ($height -eq $ExpectedHeight) -Message (
        $Message + " (height actual=" + $height + " expected=" + $ExpectedHeight + ")")
    Assert-Condition -Condition ($bands -eq $ExpectedBands) -Message (
        $Message + " (bands actual=" + $bands + " expected=" + $ExpectedBands + ")")
}

# R2 (batch D): reader for the top-level "Overviews: <n>" line that raster_inspect info prints from
# GDALRasterBand::GetOverviewCount() on band 1.
# WHY a dedicated reader instead of Assert-TextContains on "Overviews: 3": the substring form is also
# satisfied by "Overviews: 30" and it never compares the number, so it cannot tell three overview
# levels from five. Here the line must parse as a FULL integer (trailing junk = no match = fail
# closed), and the caller compares integers, so "one level dropped" and "levels ignored" are both red.
function Get-InfoOverviewCount {
    param(
        [string]$Text
    )

    $match = [regex]::Match($Text, "(?m)^\s*Overviews:\s*(\d+)\s*$")
    Assert-Condition -Condition ($match.Success) -Message (
        "raster_inspect info has no parseable 'Overviews: <int>' line")
    return [int]$match.Groups[1].Value
}

# R2 (batch D): band-aware reader for the NON-numeric labels inside a "Band N:" block -- Type and
# Color. Get-InfoBandStatValue can only serve numeric lines (its pattern demands a number and
# end-of-line), and the dtype / colour-interpretation facts are exactly the two the colormap product
# carries outside its statistics, so they need their own reader. The block anchor is the same
# "(?ms)Band N: ... up to the next Band block or end of text" that Get-InfoBandStatValue uses; it is
# repeated rather than factored out on purpose, so changing this reader cannot silently move an
# already-GOLD branch's band lookup (and the reverse: that one is not in this knife's scope).
# Fails closed when the block is missing, when the label line is missing, and when the captured
# value is empty -- "no Color line printed" is a product state, not a zero.
function Get-InfoBandLabelText {
    param(
        [string]$Text,
        [int]$Band,
        [string]$Label
    )

    $blockPattern = "(?ms)^\s*Band " + $Band + "\s*:.*?(?=^\s*Band \d+:|\Z)"
    $block = [regex]::Match($Text, $blockPattern)
    Assert-Condition -Condition ($block.Success) -Message ("raster_inspect info has no 'Band " + $Band + "' block")
    $labelPattern = "(?m)^\s*" + $Label + ":\s*(\S.*?)\s*$"
    # NOTE the different name: PowerShell variables are case-INsensitive, so "$label" here would be
    # the "[string]$Label" parameter above, and "$label.Success" would read a property off that
    # string and hand Assert-Condition an empty value instead of a boolean (this was the actual
    # first-run failure of this reader).
    $labelMatch = [regex]::Match($block.Value, $labelPattern)
    Assert-Condition -Condition ($labelMatch.Success) -Message (
        "band " + $Band + " has no '" + $Label + "' line (block read: " + $block.Value.Trim() + ")")
    $value = $labelMatch.Groups[1].Value.Trim()
    Assert-Condition -Condition ($value.Length -gt 0) -Message ("band " + $Band + " has an empty '" + $Label + "' value")
    return $value
}

# D1-C1 (Task 18): read one NAMED raster of a directory (split tiles are judged by their name,
# not by Get-ChildItem order -- the old "$tiles[0]" was alphabetical luck, so renaming the grid
# would silently move the judgement to another tile).
function Get-NamedRasterInfo {
    param(
        [string]$ResolvedCliPath,
        [string]$Directory,
        [string]$FileName
    )

    $path = Join-Path $Directory ($FileName + ".tif")
    Assert-Condition -Condition (Test-Path $path) -Message ("missing raster: " + $path)
    return Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
        "raster_inspect", "info", ("--input=" + $path)
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
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "ndvi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 24 x 1 bands" -Message "spindex_ndvi raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   0.75" -Message "spindex_ndvi mean mismatch"
        }
        "spindex_ndmi" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "ndmi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Mean:   -0.125" -Message "spindex_ndmi mean mismatch"
        }
        "spindex_bsi" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "bsi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Mean:   0" -Message "spindex_bsi mean mismatch"
        }
        "spindex_evi2" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "evi2_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Mean:   1.57895" -Message "spindex_evi2 mean mismatch"
        }
        "raster_inspect_histogram" {
            Mark-Judged -Name $Case.Name
            # PAM sidecar: analysis_input.tif.aux.xml caches the band statistics (and the last
            # histogram) by file name. A cache hit makes the plugin's ComputeStatistics return the
            # stored min/max WITHOUT reading pixels, so "min >= max -> which bucket edges" changes
            # would not move this branch. Delete it first: judge what the pixels say now.
            Remove-Item ($data.AnalysisRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "histogram_output.json")
            # R1 (batch D): the two lines this branch used to carry were "total -gt 0" and
            # "bin count -eq 16", which only prove the action emitted something. The input is
            # makeAnalysisRaster band 1, z(x, y) = x + y on a 32 x 32 grid (x, y in [0, 31]),
            # so every number below is derived from that literal:
            #   valid cells = 32*32 = 1024 (band has no NODATA), min = 0 at (0,0), max = 62 at
            #   (31,31); min != max -> 16 buckets of width (62-0)/16 = 3.875;
            #   the value k occurs c(k) = min(k+1, 63-k) times (diagonals of x+y);
            #   3.875 is a k/8 dyadic so the bucket edges are exact in double -- 31/3.875 = 8.0
            #   and 62/3.875 = 16.0 -- so bucket i takes the integers in [3.875i, 3.875(i+1)),
            #   except the last one: GDAL clamps the iBucket = 16 it computes for the max back
            #   into bucket 15, which is the only thing that distinguishes the TRUE passed as
            #   IncludeOutOfRange from a FALSE (that arm drops value 62 -> bucket 15 gives 10,
            #   bucket count stays 16, so nothing else in this file would see it).
            $histogramExpectedCounts = @(10, 26, 42, 58, 74, 90, 106, 90, 122, 106, 90, 74, 58, 42, 26, 10)
            # center = min + (i + 0.5) * binWidth = 3.875i + 1.9375, all of them k/16 dyadic with
            # at most four decimals, and the product prints them fixed with setprecision(4) -> the
            # emitted decimal IS the exact value, so the rounding error here is zero, not "small".
            $histogramExpectedCenters = @(
                1.9375, 5.8125, 9.6875, 13.5625, 17.4375, 21.3125, 25.1875, 29.0625,
                32.9375, 36.8125, 40.6875, 44.5625, 48.4375, 52.3125, 56.1875, 60.0625
            )
            Assert-Condition -Condition ($payload.histogram.Count -eq 16) -Message (
                "histogram bin count should be the 16 asked for (--bins=16 on a non-constant band)")
            for ($histIndex = 0; $histIndex -lt 16; $histIndex++) {
                $histBucket = $payload.histogram[$histIndex]
                Assert-Condition -Condition ($null -ne $histBucket) -Message (
                    "histogram bucket " + $histIndex + " is missing from the JSON body")
                $histCount = [double]$histBucket.count
                Assert-Condition -Condition ($histCount -eq $histogramExpectedCounts[$histIndex]) -Message (
                    "histogram bin " + $histIndex + " count mismatch (actual=" + $histCount +
                    " expected=" + $histogramExpectedCounts[$histIndex] +
                    ", hand-derived from band 1 = x + y on 32 x 32)")
                $histCenter = [double]$histBucket.center
                Assert-Condition -Condition ([math]::Abs($histCenter - $histogramExpectedCenters[$histIndex]) -le 1e-12) -Message (
                    "histogram bin " + $histIndex + " center mismatch (actual=" + $histCenter +
                    " expected=" + $histogramExpectedCenters[$histIndex] + " = 3.875*i + 1.9375)")
            }
            # Kept as the fail-fast point only: the product sums these very buckets into "total",
            # so this line is arithmetically implied by the 16 assertions above and is NOT counted
            # as an independent judgement in the report. It used to be "total -gt 0".
            Assert-Condition -Condition ($payload.total -eq 1024) -Message (
                "histogram total should be the 1024 valid pixels of the 32 x 32 band")
        }
        # R1 (batch D): this case used to sit on the leg allowlist with the reason "action is
        # PENDING ... so no numeric expectation exists anywhere yet". That reason is now false on
        # both sides, so the allowlist entry is deleted in the same commit as this branch (the
        # gate turns a now-judged-but-still-exempted case red as a stale entry).
        # Input = $data.AnalysisRaster = makeAnalysisRaster: 32 x 32 x 3 bands Float32, GeoTransform
        #   {116.0, 0.0005, 0, 40.0, 0, -0.0005}, no CRS, no NODATA on any band,
        #   band 1 = x + y, band 2 = 2x - y, band 3 = (x * y) mod 17 over x, y in [0, 31].
        # Everything below is derived from those literals; the arithmetic (and the tolerance
        # sandwich per datum, since raster_inspect info prints with the default 6 significant
        # digits and the quantum therefore moves with the magnitude) is in the R1 report.
        "raster_inspect_info" {
            Mark-Judged -Name $Case.Name
            Remove-Item ($data.AnalysisRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $data.AnalysisRaster)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 32 -ExpectedHeight 32 -ExpectedBands 3 -Message (
                "raster_inspect_info does not report the 32 x 32 x 3 bands of makeAnalysisRaster")
            # band 1 = x + y: min 0, max 62, mean (2 * 32 * 496)/1024 = 31,
            #   stddev = sqrt(E[x^2] - mean^2) = sqrt(1158656/1024 - 961) = sqrt(170.5)
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "band 1 (x + y) minimum mismatch")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 62 -Tolerance 1e-9 -Message (
                "band 1 (x + y) maximum mismatch")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected 31 -Tolerance 1e-9 -Message (
                "band 1 mean should be 31 = 32768/1024 summed diagonals")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "StdDev" -Expected ([math]::Sqrt(170.5)) -Tolerance 2e-4 -Message (
                "band 1 stddev should be sqrt(170.5), the population root of E[x^2] - mean^2")
            # band 2 = 2x - y: min -31 at (0,31), mean 15.5, stddev sqrt(4*85.25 + 85.25) =
            #   sqrt(426.25). Its max is 62 as well, which is the same number band 1 already
            #   pins in another block, so it is dropped here instead of posing as a 9th row.
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "Min" -Expected (-31) -Tolerance 1e-9 -Message (
                "band 2 (2x - y) minimum should be -31; it is the only negative statistic here")
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "Mean" -Expected 15.5 -Tolerance 1e-9 -Message (
                "band 2 mean should be 15.5 = (2*15.5) - 15.5")
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "StdDev" -Expected ([math]::Sqrt(426.25)) -Tolerance 2e-4 -Message (
                "band 2 stddev should be sqrt(426.25) = sqrt(5) * band-1-free variance multiple")
            # band 3 = (x * y) mod 17: residues 0..16 both occur (1*16 and 0*x), and 16 is the
            # highest statistic ceiling in this product, so it is unique to the third block.
            # The block header anchor is what makes this row independent of the two above.
            Assert-InfoBandStatNear -Text $info -Band 3 -Label "Max" -Expected 16 -Tolerance 1e-9 -Message (
                "band 3 ((x*y) mod 17) maximum mismatch")
        }
        # ---- R3a (batch D): raster_manage.cog value-level judgement -------------------------
        # Input = $cogInputRaster, a copy of makeStepLevelRaster: 40 x 24 x 1 Float32, plateaus
        #   {0^240, 10^480, 51^240} (n = 960). doBuildCog runs GDALTranslate with "-of COG
        #   -co COMPRESS=LZW -co BIGTIFF=IF_SAFER" hard-coded (no ParamSpec exposes any of them),
        #   and LZW/COG are LOSSLESS, so the decoded product must answer the fixture's own numbers:
        #     Mean   = (480*10 + 240*51)/960 = 17040/960 = 17.75
        #     E[x^2] = (480*100 + 240*2601)/960 = 672240/960 = 700.25
        #     Var    = 700.25 - 17.75^2 = 385.1875, StdDev = sqrt(385.1875) = 19.62619423117992
        #   Tolerance sandwich (info prints 6 significant digits, so the grid follows magnitude):
        #     Min 0 / Max 51 / Mean 17.75 print exactly => worst rounding 0, tolerance 1e-9, and the
        #       nearest defect reachable here is a one-unit plateau move (mean shifts 240/960 = 0.25)
        #       = 2.5e8x the tolerance.
        #     StdDev 19.6262 => grid 1e-4, worst rounding 5e-5, tolerance 2e-4 = 4.0x that worst case
        #       (four times, NOT "three orders of magnitude"); the nearest real defect, sample instead
        #       of population root = 19.630028595626232, sits 3.83e-3 away = 19x the tolerance.
        #   What this branch does NOT pin, stated so the next seat does not read it as cover:
        #     tiling and COMPRESS=LZW are not observable through raster_inspect info at all (measured:
        #     the product prints Driver "GTiff", Overviews 0, and no COMPRESSION line -- the string
        #     lives in the IMAGE_STRUCTURE domain, dataset level, which info never reads), and a file
        #     length proxy is a fixture accident at this size (measured: 40 x 24 x 4 = 3840 raw sample
        #     bytes versus a 4096-byte COG product, i.e. LZW saves nothing once the 512 x 512 tile
        #     header lands, so "smaller than raw" would be red for the WRONG reason on some other
        #     shape). Both options are pinned only on the fixture side, by PluginTest.RasterManageCog-
        #     Execution reading GetBlockSize() == 512 x 512 and IMAGE_STRUCTURE/COMPRESSION == "LZW".
        "raster_manage_cog" {
            Mark-Judged -Name $Case.Name
            # PAM sidecars are matched by file name and survive between runs; a cached Statistics
            # block makes ComputeStatistics answer without reading pixels, so drop it and judge the
            # pixels that are in the product now (same defence as the overviews branch).
            Remove-Item ($cogOutputRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $cogOutputRaster)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "raster_manage_cog did not carry the fixture's non-square 40 x 24 x 1 frame")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "the COG product must keep the fixture's Float32 sample type (an 8-bit lossy codec would not)")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "COG round trip moved the lower plateau of the fixture")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 51 -Tolerance 1e-9 -Message (
                "COG round trip moved the upper plateau of the fixture")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected 17.75 -Tolerance 1e-9 -Message (
                "COG band mean should be 17.75 = 17040/960 from the fixture plateaus")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "StdDev" -Expected ([math]::Sqrt(385.1875)) -Tolerance 2e-4 -Message (
                "COG band stddev should be sqrt(385.1875), the population root of E[x^2] - mean^2")
            # GeoTransform must survive GDALTranslate: the two label-anchored lines below are the
            # fixture's own corner/pixel grid (116 / 40 / 0.001 degrees, south-up), so a product
            # written without its georeference goes red here and nowhere else in this branch.
            Assert-TextContains -Text $info -Expected "  Origin:      (116, 40)" -Message (
                "COG product lost the fixture's upper-left corner")
            Assert-TextContains -Text $info -Expected "  Pixel Size:  (0.001, -0.001)" -Message (
                "COG product lost the fixture's 0.001 degree pixel grid")
        }
        # ---- R3a (batch D): raster_render.histogram_match value-level judgement --------------
        # source = makeStepLevelRaster copy (40 x 24, plateaus 0 / 10 / 51), reference =
        # makeHistMatchReferenceRaster (40 x 24, plateaus 0 / 17 / 34 / 51, 240 pixels each).
        # Both sides normalise on their own min/max with scale 255/(51-0) = 5.0 (exactly
        # representable), so the 8-bit levels are hand-countable and the whole expectation is
        # derived from the two fixtures, never read back out of the product:
        #   source u8 {0^240, 50^480, 255^240}     => CDF 0.25 (i = 0..49), 0.75 (50..254), 1.0 (255)
        #   reference u8 {0^240, 85^240, 170^240, 255^240} => CDF 0.25 (0..84), 0.5 (85..169),
        #                                                    0.75 (170..254), 1.0 (255)
        #   buildHistogramMatchLut walks a monotone cursor and advances only while
        #   referenceCdf < sourceCdf, i.e. "first reference level whose CDF is >= ", ties going to
        #   the LOWER level, empty bins carry the previous result. Applying that bin by bin:
        #     i = 0..49   srcCdf 0.25 => refCdf[0] = 0.25 already >= => lut[i] = 0
        #     i = 50..254 srcCdf 0.75 => cursor crosses 0.25 (..84) and 0.5 (85..169), stops at 170
        #     i = 255     srcCdf 1.0  => cursor crosses everything, stops at 255
        #   => product 8-bit levels {0^240, 170^480, 255^240} stored as Float32:
        #     Mean   = (480*170 + 240*255)/960 = 142800/960 = 148.75
        #     E[x^2] = (480*28900 + 240*65025)/960 = 29478000/960 = 30706.25
        #     Var    = 30706.25 - 148.75^2 = 8579.6875, StdDev = sqrt(8579.6875) = 92.626602550239312
        #   Counter-expectations from the SAME table (this is why the branch is not a tautology):
        #     flipping "<" to "<=" makes every tie jump one level up => lut[0] = 85, lut[50] = 255
        #       => Min 0 -> 85 and Mean 148.75 -> 212.5, so the Min and Mean rows go red together.
        #     skipping the matching (identity LUT, or copying sourceU8) => Mean = 88.75, red.
        #     using the source as its own reference => identity LUT again, 88.75, red (that is why
        #       the reference is a separate fixture).
        #   Tolerance sandwich: Min 0 / Max 255 / Mean 148.75 print exactly (6 significant digits
        #     cover 148.75 with two digits to spare) => worst rounding 0, tolerance 1e-9, nearest
        #     defect is the tie-rule flip 63.75 away => 6.4e10x. StdDev prints 92.6266 on a 1e-4
        #     grid => worst rounding 5e-5, tolerance 2e-4 = 4.0x, and the sample-root defect 0.030
        #     away = 150x. Both sides clear, so 2e-4 is not a padded number.
        "raster_render_histogram_match" {
            Mark-Judged -Name $Case.Name
            Remove-Item ($histMatchOutputRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $histMatchOutputRaster)
            )
            # Output geometry comes from matchedFloat.cols/rows, i.e. the SOURCE size; the reference
            # is the same size here so this also proves the product did not take the reference's frame.
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "histogram_match product must carry the source frame 40 x 24 x 1, not the reference's")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "histogram_match writes matched 8-bit values through convertTo(CV_32F), so the band must be Float32")
            # The value domain after matching is the reference's 8-bit levels, NOT radiometry:
            # Min 0 / Max 255 is the load-bearing pair for the tie rule (see the flip above).
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "histogram_match min should be lut[0] = 0 (a '<=' tie rule would give 85 here)")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 255 -Tolerance 1e-9 -Message (
                "histogram_match max should be lut[255] = 255, the top level of the reference")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected 148.75 -Tolerance 1e-9 -Message (
                "histogram_match mean should be 148.75 = 142800/960 for the derived LUT 0->0, 50->170, 255->255")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "StdDev" -Expected ([math]::Sqrt(8579.6875)) -Tolerance 2e-4 -Message (
                "histogram_match stddev should be sqrt(8579.6875) from the derived output multiset")
            Assert-TextContains -Text $info -Expected "  Origin:      (116, 40)" -Message (
                "histogram_match product lost the source corner written by copyRasterGeoMetadata")
            Assert-TextContains -Text $info -Expected "  Pixel Size:  (0.001, -0.001)" -Message (
                "histogram_match product lost the source pixel grid written by copyRasterGeoMetadata")
        }

        # ---- R2 (batch D): raster_manage.overviews value-level judgement -------------------
        # Input = $rasterManageOverviewsInput, a copy of makeStepLevelRaster: 40 x 24 x 1 Float32,
        #   three plateaus {0^240, 10^480, 51^240} in quadrants (x split at 20, y split at 12).
        # The action has NO output key -- GDALDataset::BuildOverviews writes the overview IFDs INTO
        # the file named by --input, so the judgement has two sides: the overview count that got
        # added, and the full-resolution base that must NOT have changed.
        # levels literal "2 1 4 0 8": doBuildOverviews keeps only tokens > 1, so 2/4/8 survive.
        #   count 3 => pins the >1 filter (unfiltered would be 5) AND pins that --levels is read at
        #   all (ignoring it falls back to the spec default "2 4 8 16" = 4).
        # Size 40 x 24 is asserted on a NON-SQUARE base on purpose: width and height are compared
        #   separately by Assert-InfoSizeExact, and a square product is an x/y reflexive invariant.
        "raster_manage_overviews" {
            Mark-Judged -Name $Case.Name
            # PAM sidecars match by file name and survive across runs; a cached Statistics block
            # makes ComputeStatistics answer without re-reading pixels, so the sidecar is dropped
            # first and the numbers below are computed fresh (same defence as raster_inspect_info).
            Remove-Item ($rasterManageOverviewsInput + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $rasterManageOverviewsInput)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "raster_manage_overviews damaged the 40 x 24 x 1 full-resolution base")
            $overviewCount = Get-InfoOverviewCount -Text $info
            Assert-Condition -Condition ($overviewCount -eq 3) -Message (
                "'2 1 4 0 8' has exactly three tokens above 1 (2, 4, 8) so the overview count must be 3 (actual=" + $overviewCount + ")")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "in-place overview build must not restype the base band away from Float32")
            # Base statistics must be untouched by an in-place build. Hand-derived from the fixture:
            #   Mean   = (480*10 + 240*51)/960 = 17040/960 = 17.75
            #   E[x^2] = (480*100 + 240*2601)/960 = 672240/960 = 700.25
            #   Var    = 700.25 - 17.75^2 = 700.25 - 315.0625 = 385.1875
            #   StdDev = sqrt(385.1875) = 19.62619423117992 (GDAL prints the POPULATION root)
            # Tolerance sandwich, this branch only (info prints 6 significant digits, so the
            # quantisation grid follows the MAGNITUDE of each value, not a fixed step):
            #   Min 0 / Max 51  -> magnitude 0 / 5.1e1, whole numbers, "0" and "51" print exactly
            #                      => worst rounding 0, tolerance 1e-9, nearest defect is a one-unit
            #                         plateau move (51 -> 50) => 1e9x margin on both sides.
            #   Mean 17.75      -> magnitude 1.775e1, needs 4 significant digits <= 6 => prints
            #                      "17.75", ZERO rounding; nearest defect that keeps Min/Max green
            #                      is the middle plateau moving by 1 => mean shifts 480/960 = 0.5
            #                      => 5e8x the tolerance.
            #   StdDev 19.6262  -> magnitude 1.96e1 => 6 significant digits put the last one in the
            #                      1e-4 place => grid 1e-4, WORST rounding 5e-5; the value actually
            #                      printed here is 19.6262 => observed residual 5.77e-6.
            #                      Tolerance 2e-4 = 4.0x the worst-case rounding (not the observed
            #                      one) and the nearest real defect, sample instead of population
            #                      stddev sqrt(385.1875*960/959) = 19.63642, is 0.0102 away = 51x
            #                      the tolerance. Both sides clear, so 2e-4 is not a padded number.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "overview build changed the lower plateau of the full-resolution band")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 51 -Tolerance 1e-9 -Message (
                "overview build changed the upper plateau of the full-resolution band")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected 17.75 -Tolerance 1e-9 -Message (
                "overview band mean should be 17.75 = 17040/960 from the fixture plateaus")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "StdDev" -Expected ([math]::Sqrt(385.1875)) -Tolerance 2e-4 -Message (
                "overview band stddev should be sqrt(385.1875), the population root of E[x^2] - mean^2")
        }
        # ---- R2 (batch D): raster_render.colormap value-level judgement --------------------
        # Input = makeStepLevelRaster (40 x 24 x 1, plateaus 0/10/51), cmap = autumn.
        # autumn's 256-entry OpenCV LUT was measured with a product-independent oracle (python + cv2,
        # all 256 indices walked): B == 0 constant, G == index exactly, R == 255 constant. The
        # plugin reorders BGR -> RGB before writing, so in the PRODUCT the bands come out as
        #   band 1 (Red)   = 255 constant
        #   band 2 (Green) = the 8-bit normalised level ITSELF
        #   band 3 (Blue)  = 0 constant
        # i.e. band 2 is a read-out of cv::normalize and every expected number is hand-derived from
        # the fixture: scale = 255/(51-0) = 5.0 (exactly representable, and 10*5.0 / 51*5.0 do not
        # straddle a round-versus-truncate boundary) => levels 0 -> 0, 10 -> 50, 51 -> 255.
        #   Mean   = (480*50 + 240*255)/960 = 85200/960 = 88.75
        #   E[x^2] = (480*2500 + 240*65025)/960 = 16806000/960 = 17506.25
        #   Var    = 17506.25 - 88.75^2 = 17506.25 - 7876.5625 = 9629.6875
        #   StdDev = sqrt(9629.6875) = 98.1309711558996
        # Dropping the BGR->RGB swap would move the constant 255 to band 3 and the constant 0 to
        # band 1 => the two constant-band rows go red together; it would NOT move band 2 (autumn's
        # G is the middle channel either way), which is why the swap evidence here is carried by the
        # constants and by the Color names, not by the ramp.
        "raster_manage_zonal_stats" {
            Mark-Judged -Name $Case.Name
            # Everything below was derived by hand BEFORE the first read; the archived derivation and
            # the produced CSV are under build/tmp/batchD/r3b/arms/ (gitignored), so the numbers are
            # restated here as well.
            #   input  = a copy of makeAnalysisRaster: 32 x 32 x 3 Float32, GeoTransform
            #            {116.0, 0.0005, 0.0, 40.0, 0.0, -0.0005}, band1 = x + y, band2 = 2x - y,
            #            band3 = (x*y) % 17, stored at index y*32 + x, and NO NODATA value.
            #   zones  = the GeoJSON rectangle above: x in [116.0001, 116.0019],
            #            y in [39.9989, 39.9999]; "zone" is read through feature_id_field, so the
            #            feature_id column pins that parameter rather than the layer FID.
            #   window = max(0, trunc((116.0001-116)/0.0005)) = trunc(0.2) = 0 for colStart,
            #            min(31, trunc(3.8)) = 3 for colEnd, max(0, trunc(0.2)) = 0 for rowStart and
            #            min(31, trunc(2.2)) = 2 for rowEnd -> a 4 x 3 sub-window. Its subGT equals
            #            the parent GeoTransform because colStart == rowStart == 0, so THIS branch
            #            cannot judge a missing sub-window translation (the unit block's zA does).
            #   mask   = pixel CENTRES inside the ring: row 2's centre is 40 - 2.5*0.0005 = 39.99875
            #            which is below MinY 39.9989, so cols 0..3 x rows 0..1 burn = 8 cells.
            #            Taking the envelope instead of the mask would give 12 cells -- that is the
            #            load-bearing difference, and the margins to the ring edges are 0.00015 map
            #            units (0.3 pixel), far outside any double rounding of 0.2 / 3.8 / 2.2.
            #   stats  = the product's population variance sumSq/count - mean^2, then sqrt when > 0:
            #            band1 {0,1,2,3 / 1,2,3,4}  -> sum 16, min 0,  max 4, mean 2,    var 1.5
            #            band2 {0,2,4,6 / -1,1,3,5} -> sum 20, min -1, max 6, mean 2.5,  var 5.25
            #            band3 {0,0,0,0 / 0,1,2,3}  -> sum 6,  min 0,  max 3, mean 0.75, var 1.1875
            #            stddev = sqrt(1.5) = 1.2247448714, sqrt(5.25) = 2.2912878475,
            #            sqrt(1.1875) = 1.0897247359. The sample form (divide by n-1) would print
            #            1.30931 / 2.44949 / 1.16496 -- at least 0.075 away, so these rows do pin
            #            population versus sample. Printing is std::ostream default = 6 SIGNIFICANT
            #            digits, which is why the expectations are written as the printed text.
            # WHY whole rows and not just the numbers: the row also pins the band column (all three
            # bands are read whatever the "band" key says) and the feature_id column.
            # NOTE on row order: the product accumulates into std::unordered_map, so row order is not
            # guaranteed. There is exactly ONE feature here, hence exactly one fid, so "which physical
            # row" is not a degree of freedom for this branch; a multi-zone product must be selected
            # by feature_id (the unit block does that).
            Assert-Condition -Condition (Test-Path $zonalOutputCsv) -Message (
                "raster_manage_zonal_stats produced no CSV at " + $zonalOutputCsv)
            $zonalCsvLines = @(Get-Content $zonalOutputCsv -Encoding UTF8)
            Assert-Condition -Condition ($zonalCsvLines.Count -eq 4) -Message (
                "raster_manage_zonal_stats must emit 1 header + 3 band rows, found " + $zonalCsvLines.Count)
            Assert-Condition -Condition ($zonalCsvLines[0] -eq "feature_id,band,min,max,mean,stddev,sum,count") -Message (
                "raster_manage_zonal_stats CSV header drifted from the product's literal")
            Assert-Condition -Condition ($zonalCsvLines -contains "z1,1,0,4,2,1.22474,16,8") -Message (
                "raster_manage_zonal_stats band 1 (x+y) row mismatch: expected the hand-derived 8-cell zone")
            Assert-Condition -Condition ($zonalCsvLines -contains "z1,2,-1,6,2.5,2.29129,20,8") -Message (
                "raster_manage_zonal_stats band 2 (2x-y) row mismatch: expected min -1 and stddev sqrt(5.25)")
            Assert-Condition -Condition ($zonalCsvLines -contains "z1,3,0,3,0.75,1.08972,6,8") -Message (
                "raster_manage_zonal_stats band 3 ((x*y)%17) row mismatch: expected sum 6 and stddev sqrt(1.1875)")
        }
        "raster_render_colormap" {
            Mark-Judged -Name $Case.Name
            Remove-Item ($rasterRenderColormapOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $rasterRenderColormapOutput)
            )
            # drv->Create(output, colored.cols, colored.rows, 3, GDT_Byte) reads width and height
            # => non-square makes (cols, rows) versus (rows, cols) a visible difference.
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 3 -Message (
                "raster_render_colormap did not write the 40 x 24 x 3 bands of the autumn product")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 2 -Label "Type") -eq "Byte") -Message (
                "colormap band 2 must be 8-bit (GDT_Byte), not a wider dtype")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Color") -eq "Red") -Message (
                "colormap band 1 colour interpretation must be Red (GCI_RedBand + 0)")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 2 -Label "Color") -eq "Green") -Message (
                "colormap band 2 colour interpretation must be Green (GCI_RedBand + 1)")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 3 -Label "Color") -eq "Blue") -Message (
                "colormap band 3 colour interpretation must be Blue (GCI_RedBand + 2)")
            # Constant bands: Min and Max are the two endpoints that pin "constant AND which
            # constant". Mean/StdDev are NOT asserted because min == max already implies mean == that
            # value and stddev == 0 -- printing them would be a corollary row, not a judgement.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 255 -Tolerance 1e-9 -Message (
                "autumn red channel must be the constant 255; if the BGR->RGB swap was dropped this is 0")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 255 -Tolerance 1e-9 -Message (
                "autumn red channel must be constant, so max equals min (255)")
            Assert-InfoBandStatNear -Text $info -Band 3 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "autumn blue channel must be the constant 0; if the BGR->RGB swap was dropped this is 255")
            Assert-InfoBandStatNear -Text $info -Band 3 -Label "Max" -Expected 0 -Tolerance 1e-9 -Message (
                "autumn blue channel must be constant, so max equals min (0)")
            # Ramp band (Green = the normalised level). Tolerance sandwich, per data point, on the
            # 6-significant-digit print of raster_inspect info:
            #   Min 0 / Max 255 -> whole numbers ("0", "255") => zero rounding; the nearest defect
            #                      is a one-unit shift of the top level (255 -> 254) => 1e9x 1e-9.
            #   Mean 88.75      -> magnitude 8.875e1, 4 significant digits <= 6 => prints "88.75",
            #                      ZERO rounding; keeping Min/Max green while moving the middle
            #                      plateau one level shifts mean by 480/960 = 0.5 => 5e8x 1e-9.
            #   StdDev 98.131   -> magnitude 9.81e1 => the 6th significant digit sits in the 1e-4
            #                      place => grid 1e-4, WORST rounding 5e-5; the value printed here
            #                      is 98.131 => observed residual 2.88e-5 (0.14x the tolerance).
            #                      Tolerance 2e-4 = 4.0x the worst-case grid. Nearest defect: the
            #                      middle plateau rendered one level off (50 -> 51) gives
            #                      sqrt(9591.1875) = 97.9346, i.e. 0.196 away = 982x the tolerance,
            #                      and it moves Mean by 0.5 too -- but with DIFFERENT residuals, so
            #                      the two rows are not the same fact written twice.
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "colormap green channel is the normalised level, so its floor must be alpha = 0")
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "Max" -Expected 255 -Tolerance 1e-9 -Message (
                "colormap green channel is the normalised level, so its ceiling must be beta = 255")
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "Mean" -Expected 88.75 -Tolerance 1e-9 -Message (
                "colormap green mean should be 88.75 = 85200/960 from levels 0/50/255 with counts 240/480/240")
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "StdDev" -Expected ([math]::Sqrt(9629.6875)) -Tolerance 2e-4 -Message (
                "colormap green stddev should be sqrt(9629.6875) = 98.1309711558996")
        }
        # ---- D1-T1: terrain value-level judgements (batch D first promotion knife) ----
        # Input for all six blocks below is the generated terrain DEM.  The citation is given by
        # FUNCTION NAME plus the literal text it writes, never by line number (R-D34): a bare
        # ":595" here survived its own move in this very file pair, because the line it drifted to
        # is another fixture in the same helper that also uses 0.0005 -- a stale anchor that keeps
        # self-confirming is worse than no anchor at all.
        #   makeTerrainRaster in tests/gui_test_data_helper.cpp writes:
        #     driver->Create(outputPath.c_str(), 48, 48, 1, GDT_Float32, nullptr)
        #     dem[y * 48 + x] = x * 1.5 + y * 0.8 + ((x * y) % 11) * 0.2
        #     double geotransform[6] = { 116.0, 0.0005, 0.0, 40.0, 0.0, -0.0005 }   // cell 0.0005
        #   and that function calls SetGeoTransform only -- no SRS/projection call and no
        #   SetNoDataValue on the band, so the DEM arrives with no CRS and no source NODATA.
        # Every expected number below is produced by an independent reference implementation of the
        # documented formulas (Horn first differences + GDAL slope/aspect/hillshade, Riley TIN TRI,
        # center-minus-8-neighbour-mean TPI, cv::Laplacian ksize=3 = d2/dx2 + d2/dy2), not read back
        # from the product. Reference and product agreed to float32 precision at the probe stage.
        # The tolerance (1e-3 .. 5e-3) only absorbs float32/double tail; any algorithm-constant change
        # moves these numbers by >= 0.5, which is why they are load-bearing.
        "terrain_slope" {
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
            #   drop -alt (GDAL default = 45)     23.863422  (controller 2026-10-28 measured the same
            #     direction with raster_inspect: Mean 174.865 at alt=35 -> 151.12 at alt=45, and
            #     omitting --altitude gives 151.12 exactly, which is what pins 45 as the default)
            #   altitude 35 -> 30                  9.981752  (Mean 184.837; this was the number the
            #     previous line mislabelled as "drop -alt", which is why the arm looked unmeasurable)
            #   aspect written as 360-aspect     181.0442     cos(az-aspect) replaced by 1: 34.3464
            #     (both are reference-model numbers computed WITHOUT the 0..255 clamp the product
            #     applies; they are kept as sensitivity ordering only, not as product readings)
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
        # Same generated DEM as the six blocks above -- cited by function name and by the literal
        # text it writes, not by line number (R-D34; see the note above the D1-T1 block):
        # makeTerrainRaster in tests/gui_test_data_helper.cpp, Create(path, 48, 48, 1, GDT_Float32),
        # dem[y*48 + x] = x*1.5 + y*0.8 + ((x*y)%11)*0.2, geotransform
        # {116.0, 0.0005, 0.0, 40.0, 0.0, -0.0005}, no SRS call and no band SetNoDataValue.
        # These three
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "terrain_viewshed_multi_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Min:    0" -Message "terrain_viewshed_multi min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    255" -Message "terrain_viewshed_multi max mismatch"
        }
        "projection_info" {
            Mark-Judged -Name $Case.Name
            $text = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "projection", "info", ("--input=" + $data.ClassificationRaster)
            )
            Assert-TextContains -Text $text -Expected "Size: 6 x 6 x 1 bands" -Message "projection_info size mismatch"
            Assert-TextContains -Text $text -Expected "Authority: EPSG:3857" -Message "projection_info authority mismatch"
            # D1-P: two more readings that are hand-derivable from the generator, not back-filled
            # from the product. makeClassRaster (tests/gui_test_data_helper.cpp) creates this input as
            # 6 x 6 x 1 Int32 with geotransform {12935000.0, 50.0, 0.0, 4852300.0, 0.0, -50.0}, so
            #   Pixel Size printed by doInfo = "50" and "-50"  (adfGT[1], adfGT[5])
            #   Origin printed by doInfo = adfGT[0], adfGT[3] = 12935000, 4852300, and doInfo writes the
            #     message with an ostringstream at default precision (6 significant digits), which renders
            #     those two as 1.2935e+07 and 4.8523e+06.
            # The derived envelope is x in [12935000, 12935000 + 6*50 = 12935300],
            # y in [4852300 - 6*50 = 4852000, 4852300] - the same box as the generator's polygon ring.
            # GAP (batch D D1-P fix round 1, corrects what an earlier comment here claimed): doInfo emits
            # no extent key and prints no envelope, and the Debug unit case
            # PluginTest.ProjectionInfoExecution does NOT pin the four sides either - its right/bottom rows
            # were only origin + n*pixelSize built from the values its Origin/Pixel Size rows already pin,
            # so with those rows green the difference is exactly zero and they can never go red on their
            # own. The four sides are therefore judged on NEITHER side today and are registered as a gap;
            # the unit case pins Origin, Pixel Size and Rotation (doInfo prints all three pairs).
            Assert-TextContains -Text $text -Expected "Pixel Size: (50, -50)" -Message "projection_info pixel size mismatch"
            Assert-TextContains -Text $text -Expected "Origin: (1.2935e+07, 4.8523e+06)" -Message "projection_info origin mismatch"
        }
        "projection_transform" {
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "projection_assign_input.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 24 x 6 bands" -Message "projection_assign_srs raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "projection_assign_srs crs mismatch"
        }
        "projection_reproject" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "projection_reproject_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   7 x 5 x 1 bands" -Message "projection_reproject raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "projection_reproject crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   1" -Message "projection_reproject mean mismatch"
        }
        "cutting_clip" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "cutting_clip_output.tif"))
            )
            # ---- D1-C1 (Task 18): value-level readings, DERIVED not measured ----
            # Source fixture (gui_test_data_helper.cpp makeClassRaster, reached through
            #   makeClassificationInputs): 6 x 6 x 1 Int32, EPSG:3857, geotransform origin
            #   (12935000, 4852300), pixel size 50 m, EVERY cell == 1, NoData == 0.
            # Case extent uses the axis order xmin,ymin,xmax,ymax (that is what doClip splices into
            #   GDAL's -projwin ulx uly lrx lry as [0] [3] [2] [1]): 12935050,4852100,12935150,4852200
            #   start col = (12935050 - 12935000) / 50 = 1 ; end col = (12935150 - 12935000) / 50 = 3
            #   start row = (4852300 - 4852200) / 50 = 2 ; end row = (4852300 - 4852100) / 50 = 4
            #   => Width = 2, Height = 2, and the window lies wholly inside the source.
            # WHAT THIS BRANCH CANNOT JUDGE (registered as a leg-side gap, do not read it as
            #   position evidence): every valid cell of this fixture is 1, so Min/Max/Mean/StdDev are
            #   the same for ANY window that stays inside the raster -- they pin "the band is the
            #   source's data and it is constant", not "where the window is". Window POSITION is
            #   pinned on the unit side only (tests/test_plugins.cpp CuttingClipByExtentProducesExpectedWindow
            #   reads the output geotransform as doubles). Width/Height are the leg's shape evidence,
            #   and a square 2 x 2 is itself x/y reflexive -- that is why the asymmetric twin case
            #   cutting_clip_asymmetric_window below exists.
            Assert-InfoSizeExact -Text $info -ExpectedWidth 2 -ExpectedHeight 2 -ExpectedBands 1 -Message "cutting_clip window shape"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:3857" -Message "cutting_clip crs mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 1.0 -Tolerance 1e-6 -Message "cutting_clip band 1 mean"
            Assert-InfoStatNear -Text $info -Label "StdDev" -Expected 0.0 -Tolerance 1e-9 -Message "cutting_clip band 1 stddev"
        }
        "cutting_clip_asymmetric_window" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "cutting_clip_asym_output.tif"))
            )
            # Same fixture as cutting_clip, non-square extent 12935000,4852100,12935150,4852300:
            #   cols (12935000 - 12935000)/50 = 0 .. (12935150 - 12935000)/50 = 3 => Width = 3
            #   rows (4852300 - 4852300)/50 = 0 .. (4852300 - 4852100)/50 = 4 => Height = 4
            # W != H is the whole point of this case: an axis-order mistake (feeding the extent as
            # {xmin,xmax,ymin,ymax} or dropping the y sign) can no longer hide behind a square window.
            Assert-InfoSizeExact -Text $info -ExpectedWidth 3 -ExpectedHeight 4 -ExpectedBands 1 -Message "cutting_clip_asymmetric window shape"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 1.0 -Tolerance 1e-6 -Message "cutting_clip_asymmetric band 1 mean"
            Assert-InfoStatNear -Text $info -Label "StdDev" -Expected 0.0 -Tolerance 1e-9 -Message "cutting_clip_asymmetric band 1 stddev"
        }
        "cutting_mosaic" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "cutting_mosaic_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   6 x 6 x 1 bands" -Message "cutting_mosaic raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:3857" -Message "cutting_mosaic crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   1" -Message "cutting_mosaic mean mismatch"
        }
        "cutting_split" {
            Mark-Judged -Name $Case.Name
            $tilesDir = Join-Path $ResolvedOutputRoot "cutting_split_tiles"
            $tiles = @(Get-ChildItem $tilesDir -Filter *.tif -ErrorAction Stop)
            # doSplit's REAL grid (read of the plugin's loops, and reproduced on copies): the stride is
            #   tile_size (xOff += tileSize), the block size is min(tile_size + overlap, remaining),
            #   the block NAME is tile_<xOff/tile_size>_<yOff/tile_size>, and the COUNT does not depend
            #   on overlap at all. With the fixture 6 x 6 and tile_size 4:
            #   count = ceil(6/4) * ceil(6/4) = 2 * 2 = 4
            #   tile_0_0 (xOff 0, yOff 0) = min(4,6) x min(4,6) = 4 x 4
            #   tile_1_0 (xOff 4, yOff 0) = min(4,2) x min(4,6) = 2 x 4   <- the pair with tile_0_1
            #   tile_0_1 (xOff 0, yOff 4) = min(4,6) x min(4,2) = 4 x 2      is what pins the axes
            #   tile_1_1 (xOff 4, yOff 4) = min(4,2) x min(4,2) = 2 x 2   <- last block clamp
            Assert-Condition -Condition ($tiles.Count -eq 4) -Message "cutting_split tile count mismatch"
            $tile00 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_0_0"
            Assert-InfoSizeExact -Text $tile00 -ExpectedWidth 4 -ExpectedHeight 4 -ExpectedBands 1 -Message "cutting_split tile_0_0 shape"
            $tile10 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_1_0"
            Assert-InfoSizeExact -Text $tile10 -ExpectedWidth 2 -ExpectedHeight 4 -ExpectedBands 1 -Message "cutting_split tile_1_0 shape"
            $tile01 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_0_1"
            Assert-InfoSizeExact -Text $tile01 -ExpectedWidth 4 -ExpectedHeight 2 -ExpectedBands 1 -Message "cutting_split tile_0_1 shape"
            $tile11 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_1_1"
            Assert-InfoSizeExact -Text $tile11 -ExpectedWidth 2 -ExpectedHeight 2 -ExpectedBands 1 -Message "cutting_split tile_1_1 shape"
            # Content level on the first tile: constant 1 fixture, so this is "the tile carries the
            # source values", not a position read (same limit as the clip branch above).
            Assert-InfoStatNear -Text $tile00 -Label "Mean" -Expected 1.0 -Tolerance 1e-6 -Message "cutting_split tile_0_0 mean"
        }
        "cutting_split_overlap" {
            Mark-Judged -Name $Case.Name
            $tilesDir = Join-Path $ResolvedOutputRoot "cutting_split_overlap_tiles"
            $tiles = @(Get-ChildItem $tilesDir -Filter *.tif -ErrorAction Stop)
            # This is the ONLY leg-side branch that consumes overlap > 0. Same 6 x 6 fixture,
            #   tile_size 4, overlap 2 => count stays 4 (overlap never enters the stride), while the
            #   blocks grow to min(4+2, remaining):
            #   tile_0_0 = 6 x 6   (4+2 clamped by the raster edge -- 2 x 2 more than the ov=0 case)
            #   tile_1_0 = 2 x 6   tile_0_1 = 6 x 2   tile_1_1 = 2 x 2
            # WARNING: tile_1_1 is IDENTICAL to the ov=0 case (min(clip, remaining) eats the overlap first),
            #   so the last block is NOT evidence about overlap -- if overlap were ignored this branch
            #   stays green on tile_1_1 and goes red on tile_0_0 / tile_1_0 / tile_0_1 only.
            Assert-Condition -Condition ($tiles.Count -eq 4) -Message "cutting_split_overlap tile count mismatch"
            $tile00 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_0_0"
            Assert-InfoSizeExact -Text $tile00 -ExpectedWidth 6 -ExpectedHeight 6 -ExpectedBands 1 -Message "cutting_split_overlap tile_0_0 shape"
            $tile10 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_1_0"
            Assert-InfoSizeExact -Text $tile10 -ExpectedWidth 2 -ExpectedHeight 6 -ExpectedBands 1 -Message "cutting_split_overlap tile_1_0 shape"
            $tile01 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_0_1"
            Assert-InfoSizeExact -Text $tile01 -ExpectedWidth 6 -ExpectedHeight 2 -ExpectedBands 1 -Message "cutting_split_overlap tile_0_1 shape"
            $tile11 = Get-NamedRasterInfo -ResolvedCliPath $ResolvedCliPath -Directory $tilesDir -FileName "tile_1_1"
            Assert-InfoSizeExact -Text $tile11 -ExpectedWidth 2 -ExpectedHeight 2 -ExpectedBands 1 -Message "cutting_split_overlap tile_1_1 shape"
        }
        "cutting_tile" {
            Mark-Judged -Name $Case.Name
            # ---- D1-C2 (batch D promotion): value-level readings for cutting tile, DERIVED not measured ----
            # The input is $data.ClassificationRaster: 6 x 6 x 1 EPSG:3857 with
            #   GeoTransform {12935000, 50, 0, 4852300, 0, -50} (its own raster_inspect info prints
            #   Origin (1.2935e+07, 4.8523e+06) and Pixel Size (50, -50)) => the envelope is
            #   x in [12935000, 12935300], y in [4852000, 4852300] and doTile reads pixelSize = 50.
            # Expectations use the spherical Web Mercator constants only:
            #   2*pi*a = 40075016.685578488 (a = 6378137), tileWidthM(z) = 2*pi*a / 2^z,
            #   res(z, ts) = tileWidthM(z) / ts, and the auto level is the first z from 30 downwards with
            #   res(z, ts) >= pixelSize * 0.5 = 25:
            #     ts=256: res(12) = 38.218514 >= 25 while res(13) = 19.109257 < 25  => max_zoom = 12
            #     ts=128: res(13) = 38.218514 >= 25 while res(14) = 19.109257 < 25  => max_zoom = 13
            #   min_zoom keeps its plugin default 0, and 300 m < tileWidthM(z) for every z <= 13, so each
            #   level holds exactly one tile => level COUNT = max_zoom + 1 (13 and 14).
            # Tile indices come from the same metric derivation as the plugin's levelTileBounds:
            #   x = floor((12935000 + 20037508.342789244) / tileWidthM(z))
            #   y = floor((20037508.342789244 - 4852300) / tileWidthM(z))
            #   z=12 => x = 3370, y = 1552    z=13 => x = 6740, y = 3104
            # x != y on this fixture (unlike the square unit-test fixture), so a wrong axis order cannot
            #   hide behind the names here either.
            # WHY --skip_blank=false: the plugin default (true) drops tiles with at most 4 opaque pixels,
            #   which is what the LOW levels of this 300 m fixture produce (z=9 -> 2x2 = 4 pixels).
            #   That tolerance is judged by PluginTest.TileBlankGuardsAndSkipBlankCombination; leaving it
            #   on would mix a blank-guard branch into the level COUNT asserted below.
            $tileRoot = $script:CuttingTileRoot
            $levels = @(Get-ChildItem $tileRoot -Directory -ErrorAction Stop | ForEach-Object { [int]$_.Name })
            $levelText = (@($levels | Sort-Object) -join ",")
            Assert-Condition -Condition ($levelText -eq "0,1,2,3,4,5,6,7,8,9,10,11,12") -Message (
                "cutting_tile level set mismatch (actual=[" + $levelText + "] expected=[0..12], derived max_zoom=12 for pixelSize=50 with tile_pixel_size=256)")
            $deepPath = Join-Path $tileRoot "12\3370\1552.png"
            Assert-Condition -Condition (Test-Path $deepPath) -Message ("cutting_tile missing the derived deepest tile: " + $deepPath)
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $deepPath)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 256 -ExpectedHeight 256 -ExpectedBands 4 -Message "cutting_tile ts=256 deepest tile shape"
            # Alpha (band 4) mean: the covered sub-rectangle is dstW x dstH output pixels, and
            #   dstW = ceil((12935300 - tileMinX)/res(12,256)) - floor((12935000 - tileMinX)/res(12,256))
            #   with tileMinX = -20037508.342789244 + 3370 * tileWidthM(12) = 12934368.097...
            #   => floor(16.533) = 16 and ceil(24.382) = 25 => dstW = dstH = 9 (both axes carry the same
            #   300 m span, so the row count follows the same arithmetic).
            # Every covered pixel gets alpha 255 and the rest stay 0 => mean = 255 * 81 / (256 * 256).
            # Tolerance 1e-5 is ABSOLUTE and the argument must hold PER BRANCH, not once for the pair.
            #   raster_inspect info prints 6 significant digits, so for a value in [10^e, 10^(e+1)) the
            #   quantisation step is 10^(e-5) and the worst rounding error is half of that:
            #     this branch expects 0.3151702880859375 (e=-1) => step 1e-6, worst 5e-7 => 20x margin
            #       (the actually printed 0.315170 is off by 2.88e-7 => 34.7x);
            #     the ts=128 branch below expects 1.26068115234375 (e=0) => step 1e-5, worst 5e-6 =>
            #       only 2x margin (printed 1.26068 is off by 1.15e-6 => 8.7x). That is the TIGHT arm;
            #       do not read this branch's 20x as covering both (same defect class as #171 F-3).
            #   What makes the branch load-bearing is sensitivity, not margin: one pixel of drift in
            #     dstW moves the mean by 255 * 18 / 65536 = 0.070038 => 7004x the tolerance.
            Assert-InfoBandStatNear -Text $info -Band 4 -Label "Mean" -Expected (255.0 * 81.0 / 65536.0) -Tolerance 1e-5 -Message "cutting_tile ts=256 alpha mean"
            # Second pass on the same input with tile_pixel_size=128, run inside this branch because the
            #   judgement is the PAIR. res(z, ts) = 2*pi*a / (ts * 2^z), so halving ts DOUBLES res at the
            #   same z and computeMaxZoom must walk one level DEEPER (12 -> 13) to come back to the same
            #   res (res(12,256) = res(13,128) = 38.2185 m/px). Same res => the 300 m span still covers a
            #   9x9 covered block, while the container shrinks to 128*128 = 16384 pixels =>
            #   the alpha mean is QUADRUPLED (65536 / 16384 = 4), NOT doubled. A grid that hardcodes 256
            #   loses the extra level; a container that hardcodes 256 loses the x4 mean. Arms are named.
            $tileRoot128 = $script:CuttingTileTs128Root
            $runText = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "cutting", "tile",
                ("--input=" + $data.ClassificationRaster),
                ("--output=" + $tileRoot128),
                "--tile_pixel_size=128",
                "--skip_blank=false"
            )
            $levels128 = @(Get-ChildItem $tileRoot128 -Directory -ErrorAction Stop | ForEach-Object { [int]$_.Name })
            $levelText128 = (@($levels128 | Sort-Object) -join ",")
            Assert-Condition -Condition ($levelText128 -eq "0,1,2,3,4,5,6,7,8,9,10,11,12,13") -Message (
                "cutting_tile ts=128 level set mismatch (actual=[" + $levelText128 + "] expected=[0..13], derived max_zoom=13)")
            $deepPath128 = Join-Path $tileRoot128 "13\6740\3104.png"
            Assert-Condition -Condition (Test-Path $deepPath128) -Message ("cutting_tile ts=128 missing the derived deepest tile: " + $deepPath128)
            $info128 = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $deepPath128)
            )
            Assert-InfoSizeExact -Text $info128 -ExpectedWidth 128 -ExpectedHeight 128 -ExpectedBands 4 -Message "cutting_tile ts=128 deepest tile shape"
            Assert-InfoBandStatNear -Text $info128 -Band 4 -Label "Mean" -Expected (255.0 * 81.0 / 16384.0) -Tolerance 1e-5 -Message "cutting_tile ts=128 alpha mean"
            # The progress line is NOT used as evidence (same scope rule as the R-T17-8
            #   product-only judgement); this last line only records that the two passes really
            #   produced two different grids (13 levels vs 14).
            Assert-Condition -Condition ($levels128.Count - $levels.Count -eq 1) -Message (
                "cutting_tile ts=128 must add exactly one level over ts=256 (actual=" + $levels128.Count + " vs " + $levels.Count + ")")
        }
        "cutting_merge_bands" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "cutting_merge_output.tif"))
            )
            # ---- D1-C1 (Task 18): value-level readings, DERIVED not measured ----
            # Case passes --input=<ndvi_input.tif>,<ndvi_input.tif>. That fixture
            #   (gui_test_data_helper.cpp makeNdviRaster) is 24 x 24 x 6 Float32 whose band k is a
            #   CONSTANT raster with literal values 10, 20, 30, 70, 90, 110. doMergeBands builds a
            #   "-separate" VRT over the concatenated file list, so the merged product is
            #   bands 1..6 = 10,20,30,70,90,110 and bands 7..12 = the same six numbers again.
            # Band count = 6 + 6 = 12 (list length), size stays 24 x 24 because both entries share
            #   one georeference (two DIFFERENT origins would grow the product to the union extent --
            #   seen on copies, where a 10-valued half became Mean 5 because the other half was padding).
            # Reading each band through its OWN block (Get-InfoBandStatValue) is deliberate: the old
            #   Assert-TextContains "Mean:   110" only proves some band has that text, and
            #   Get-InfoStatValue would have judged band 1 while claiming the last band.
            # WARNING: LIMITATION (leg-side gap, registered): both list entries are the SAME file, so this
            #   case cannot see WHICH FILE came first -- a reversed file list yields an identical
            #   product. File ORDER is judged on the unit side (tests/test_plugins.cpp
            #   CuttingMergeBandsOrderShuffleAndRepeatedFile, three different constants). What the leg
            #   does pin is the per-band VALUES, the band COUNT and the intra-file band expansion order.
            Assert-InfoSizeExact -Text $info -ExpectedWidth 24 -ExpectedHeight 24 -ExpectedBands 12 -Message "cutting_merge_bands product shape"
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected 10.0 -Tolerance 1e-6 -Message "cutting_merge_bands band 1 mean"
            Assert-InfoBandStatNear -Text $info -Band 2 -Label "Mean" -Expected 20.0 -Tolerance 1e-6 -Message "cutting_merge_bands band 2 mean"
            Assert-InfoBandStatNear -Text $info -Band 6 -Label "Mean" -Expected 110.0 -Tolerance 1e-6 -Message "cutting_merge_bands band 6 mean"
            Assert-InfoBandStatNear -Text $info -Band 7 -Label "Mean" -Expected 10.0 -Tolerance 1e-6 -Message "cutting_merge_bands band 7 mean"
            Assert-InfoBandStatNear -Text $info -Band 8 -Label "Mean" -Expected 20.0 -Tolerance 1e-6 -Message "cutting_merge_bands band 8 mean"
            Assert-InfoBandStatNear -Text $info -Band 12 -Label "Mean" -Expected 110.0 -Tolerance 1e-6 -Message "cutting_merge_bands band 12 mean"
            Assert-InfoBandStatNear -Text $info -Band 12 -Label "StdDev" -Expected 0.0 -Tolerance 1e-9 -Message "cutting_merge_bands band 12 stddev"
        }
        # ---- D1-F (task 15): raster_math_band_math value-level branch ----
        # Before this knife the switch had NO branch for this case name: the case ran (the CLI call
        # and the expected-output existence check happen in Invoke-Case) but nothing judged the
        # product, and because the switch has no `default` arm, deleting a branch -- or the case
        # name -- stays silently green. This block is what makes that gap closed for band_math.
        # Fixture: gui_test_data_helper.cpp makePansharpenInputs -> 30 x 30 x 3 Float32 with
        #   B1 == 30.0, B2 == 50.0, B3 == 70.0 everywhere; case args pass "--expression=B1+B2".
        # Expectations are DERIVED, not measured: doBandMath builds one CV_32F band of the same
        #   width/height (raster_math_plugin.cpp: cv::Mat result(height, width, CV_32F) and
        #   matToGdalTiff(result, input, output, 1)), and 30.0f + 50.0f == 80.0f is exact in float32,
        #   so size is 30 x 30 x 1 and every pixel is 80.
        # Min == Max == Mean pins it per-pixel: a product whose average is 80 but whose pixels are
        #   not would fail here while a mean-only pin would not.
        # Value-level on purpose: Assert-TextContains "Mean:   80" is also satisfied by 800 or 80.5
        #   (prefix eating), the shape the registry header forbids, so the numbers go through
        #   Assert-InfoStatNear, which anchors the label to its own column and parses the token.
        # Liveness proof (same knife): replacing the evaluated value with the first operand
        #   (result.at<float>(y,x) = values[0]) turns this branch red with actual=30.
        "raster_math_band_math" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "band_math_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   30 x 30 x 1 bands" -Message "raster_math_band_math raster size mismatch"
            Assert-InfoStatNear -Text $info -Label "Min" -Expected 80.0 -Tolerance 1e-06 -Message "raster_math_band_math B1+B2 min mismatch"
            Assert-InfoStatNear -Text $info -Label "Max" -Expected 80.0 -Tolerance 1e-06 -Message "raster_math_band_math B1+B2 max mismatch"
            Assert-InfoStatNear -Text $info -Label "Mean" -Expected 80.0 -Tolerance 1e-06 -Message "raster_math_band_math B1+B2 mean mismatch"
        }
        "processing_pansharpen" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "pansharpen_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   30 x 30 x 3 bands" -Message "processing_pansharpen raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   75" -Message "processing_pansharpen band1 mean mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   85" -Message "processing_pansharpen band2 mean mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   95" -Message "processing_pansharpen band3 mean mismatch"
        }
        "processing_gabor_filter" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "gabor_filter_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   32 x 32 x 1 bands" -Message "processing_gabor_filter raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_gabor_filter raster type mismatch"
        }
        "processing_glcm_texture" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "glcm_texture_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   32 x 32 x 1 bands" -Message "processing_glcm_texture raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_glcm_texture raster type mismatch"
        }
        "processing_mean_shift_filter" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "mean_shift_filter_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   32 x 32 x 1 bands" -Message "processing_mean_shift_filter raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_mean_shift_filter raster type mismatch"
        }
        "processing_skeleton" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "skeleton_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   64 x 64 x 1 bands" -Message "processing_skeleton raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_skeleton raster type mismatch"
            Assert-TextContains -Text $info -Expected "Max:    1" -Message "processing_skeleton max mismatch"
        }
        "processing_connected_components" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "connected_components_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   64 x 64 x 1 bands" -Message "processing_connected_components raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "processing_connected_components raster type mismatch"
            Assert-TextContains -Text $info -Expected "Max:    4" -Message "processing_connected_components component count mismatch"
        }
        "processing_threshold" {
            Mark-Judged -Name $Case.Name
            # Statistics are cached per file name in a PAM sidecar; this product is written by this
            # case only, but a run interrupted after the sidecar was written would let a later run
            # read the CACHED numbers instead of the pixels. Delete it, then judge the pixels.
            Remove-Item ($processingThresholdOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $processingThresholdOutput)
            )
            # WHY Assert-InfoSizeExact and not Assert-TextContains "Size:   40 x 24 x 1 bands": the
            # substring never compares the numbers and cannot tell a one-pixel window error from a
            # correct window. Width and height are asserted separately on purpose -- the fixture is
            # 40 x 24, so (cols, rows) written the other way round is only visible when W != H.
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "processing_threshold did not write the 40 x 24 x 1 band product")
            # Product dtype: convertTo(CV_32F, 1/255) plus createRaster(..., GDT_Float32). This is a
            # judgement in its own right -- 0.0 and 1.0 would also fit an 8-bit band, so the dtype
            # line is not a corollary of the four numbers below.
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "processing_threshold product must be Float32 (convertTo(CV_32F) then matToGdalTiff)")
            # Tolerance sandwich, per data point (raster_inspect info prints at six SIGNIFICANT
            # digits, so these four expectations are checked against the printed form):
            #   Min 0 / Max 1 / Mean 0.75 -- all three are exactly representable and print as whole
            #     tokens ("0", "1", "0.75"), so their rounding residual is ZERO and 1e-9 buys nothing
            #     for the print; what 1e-9 does buy is float32 read-back noise, which is <= 1 ulp.
            #   Nearest defects, all far outside 1e-9:
            #     threshold_value ignored/raised above the middle plateau (30 -> 100)  : Mean 0.25 (|d| = 0.5)
            #     the toUint8 pre-normalisation dropped (raw 0/10/51 compared to 30)   : Mean 0.25 (|d| = 0.5)
            #     max_value ignored and 255 hard-coded, or the /255 scale dropped      : Max 255 (|d| = 254)
            #     binary replaced by binary_inv                                         : Mean 0.25 AND Max/Min swap
            # So the tolerance is ~8 orders below the smallest defect it is meant to catch.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "processing_threshold low plateau (u8 0, 240 px) must survive binary as 0.0")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 1 -Tolerance 1e-9 -Message (
                "processing_threshold high plateau must be max_value/255 = 1.0, not 255 (the /255 scale is hard-coded)")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected 0.75 -Tolerance 1e-9 -Message (
                "processing_threshold mean should be 0.75 = 720/960 from the two plateaus above threshold_value=30")
            # WHY StdDev is NOT asserted here: for this product shape sigma = sqrt(p*(1-p)) with
            # p = Mean, so a StdDev row would be the same count read a second time (a corollary row,
            # not a judgement). And "the band is strictly 0/1" is not something raster_inspect info's
            # five statistics can prove -- that one is carried by the per-pixel EXPECT_NEAR probes in
            # the PluginTest.ProcessingThresholdExecution unit block, which is the leg/unit split this
            # seat registers as a coverage boundary rather than padding the leg with a duplicate row.
        }
        # ------------------------------------------------------------------------------------
        # P2b (batch D): the four processing.enhance cases registered below.
        # SHARED REASONING for all four (stated once here rather than copied four times, so a future
        # edit cannot let the copies drift apart):
        #   * size / dtype: all four products come from the SAME writer (writeMatOutput -> matToGdalTiff
        #     -> createRaster(dst, mat.cols, mat.rows, 1, GDT_Float32)), so these two rows judge the
        #     WRITER, not the enhance_type. They are asserted per branch because each branch reads a
        #     different file, but their discriminating power is one shared shape, and the per-branch
        #     load-bearing rows are the three statistics below them.
        #   * Assert-InfoSizeExact instead of Assert-TextContains "Size:   40 x 24 x 1 bands": the
        #     substring never compares the numbers, and width and height are compared SEPARATELY so an
        #     axis swap (mat.cols/mat.rows written the other way round) is visible on this non-square
        #     fixture (a square one is an x/y reflexive invariant and cannot see it).
        #   * dtype is its own judgement: 0.0 / 0.666667 / 1.0 would also fit an 8-bit band, so "Type"
        #     is not a corollary of Min/Max/Mean.
        #   * tolerance, per data point (raster_inspect info prints at SIX SIGNIFICANT digits, measured
        #     on this carrier, verbatim: "Mean:   0.75" and "StdDev: 0.433013"):
        #       - Min 0 / Max 1 are whole integers that the float32 pipeline produces EXACTLY
        #         (0*x -> 0.0; the top plateau's convertTo scale 1/255 and the normalisation x/x both
        #         land on 1.0), so their print residual is zero and 1e-9 only buys float32 read-back
        #         noise <= 1 ulp. Nearest defects are >= 2.5e-1 away => >= 2.5e8 times the tolerance.
        #       - the four Means are NOT zero-rounding quantities: at magnitude ~0.3-0.6 the six-digit
        #         step is 1e-6 and the half-step is 5e-7, so the tolerance MUST be larger than the
        #         point's own print residual. 1e-6 is used, and per branch the sandwich is written
        #         below the assertion it covers. Do not "tighten" these to 1e-9: that would make them
        #         red on the printer, not on a defect.
        #   * StdDev is not asserted in any of the four: sigma^2 = 0.25*(m0^2+m2^2) + 0.5*m1^2 - Mean^2
        #     is the same three plateau values read a second time (corollary row, same reasoning P1 used).
        #   * clahe has no branch here on purpose -- see the block comment at the New-Case sites.
        # ------------------------------------------------------------------------------------
        "processing_enhance_equalize" {
            Mark-Judged -Name $Case.Name
            Remove-Item ($processingEnhanceEqualizeOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $processingEnhanceEqualizeOutput)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "processing_enhance_equalize did not write the 40 x 24 x 1 band product")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "processing_enhance_equalize product must be Float32 (convertTo(CV_32F, 1/255) then matToGdalTiff)")
            # Sandwich, equalize: Min 0 and Max 1 are exact float32 integers -> zero print residual,
            # 1e-9 only absorbs read-back noise. Nearest defects: lut[0] not forced to 0 (the other CDF
            # convention) -> Min 0.250980 (|d| = 0.251 = 2.5e8 x tol); lut[255] = 254 instead of 255 ->
            # Max 0.996078 (|d| = 3.92e-3 = 3.9e6 x tol). SCOPE LIMIT worth stating: a ONE-ulp drift of
            # Max below 1.0 prints as "1" at six significant digits and is not resolvable on this
            # carrier at all -- that exactness is what the per-pixel unit probe (E3) is for.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "processing_enhance_equalize lowest u8 level is the first occupied one, upstream assigns lut[i0]=0")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 1 -Tolerance 1e-9 -Message (
                "processing_enhance_equalize highest u8 level must map to lut=255 -> 255/255 = 1.0 exactly")
            # Sandwich, Mean 7/12 = 0.58333333333333337: magnitude in [0.5,1) -> step 1e-6, half-step
            # 5e-7; the product's own mean is 0.5833333432 (float32 170/255 accumulated over 480 px adds
            # <= 2.5e-8), which prints 0.583333 -> |printed - expected| = 3.33e-7, worst case with the
            # float32 term 3.58e-7 (2.8 x headroom). Even if the printer rounded UP to 0.583334 the
            # distance would be 6.7e-7 (1.5 x headroom), so no false red either way. Smallest
            # distinguishable defect: the other CDF convention gives Mean 0.687255, |d| = 0.103922 =
            # 1.04e5 x the tolerance.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected (7.0 / 12.0) -Tolerance 1e-6 -Message (
                "processing_enhance_equalize mean should be 7/12 = (240*0 + 480*170/255 + 240*1)/960 (beta CDF convention)")
        }
        "processing_enhance_normalize" {
            Mark-Judged -Name $Case.Name
            Remove-Item ($processingEnhanceNormalizeOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $processingEnhanceNormalizeOutput)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "processing_enhance_normalize did not write the 40 x 24 x 1 band product")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "processing_enhance_normalize product must be Float32 (NORM_MINMAX with CV_32F as the destination type)")
            # Sandwich, normalize: Min 0 / Max 1 are the NORM_MINMAX alpha/beta landed on float32
            # exactly (51 * (1/51) is the double 0.99999999999999989, whose nearest float32 is 1.0), so
            # zero print residual and 1e-9 absorbs read-back noise only. Nearest defect: alpha/beta
            # swapped or a non-zero alpha -> Min/Max move by >= 1e-2 (>= 1e7 x tol).
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "processing_enhance_normalize raw min plateau (0, 240 px) must map to NORM_MINMAX alpha 0")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 1 -Tolerance 1e-9 -Message (
                "processing_enhance_normalize raw max plateau (51, 240 px) must map to NORM_MINMAX beta 1")
            # Sandwich, Mean 71/204 = 0.34803921568627449: magnitude in [0.1,1) -> step 1e-6, half-step
            # 5e-7. The product's own mean is 0.3480392173 (float32 10/51 adds <= 3e-9), which prints
            # 0.348039 -> |printed - expected| = 2.16e-7, worst case 2.19e-7 (4.6 x headroom); it sits
            # 2.83e-7 below the rounding midpoint 0.3480395, far outside the float32 term, so the print
            # side is not in question here. Smallest distinguishable defect on this row: the middle
            # plateau off by one 8-bit level (50/255 -> 51/255) moves the Mean by 0.5*(1/255) =
            # 1.961e-3 = 1961 x the tolerance.
            # NOTE the honest scope limit: on THIS fixture an 8-bit-detoured normalize is NOT separable,
            # because 50/255 and 10/51 are the same rational -- see the block comment at the New-Case.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected (71.0 / 204.0) -Tolerance 1e-6 -Message (
                "processing_enhance_normalize mean should be 71/204 = (480*10/51 + 240*1)/960")
        }
        "processing_enhance_log" {
            Mark-Judged -Name $Case.Name
            Remove-Item ($processingEnhanceLogOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $processingEnhanceLogOutput)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "processing_enhance_log did not write the 40 x 24 x 1 band product")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "processing_enhance_log product must be Float32 (cv::log on CV_32F then the min/max gate)")
            # Sandwich, log Min/Max: ln(0+1) = 0 and the gate divides maxVal by itself -> exactly 1.0 in
            # float32, so zero print residual and 1e-9 absorbs read-back noise only. Max is the load
            # bearing one here: if the (maxVal-minVal) > 1e-10 normalisation were skipped, Max would be
            # ln(52) = 3.9512437 -> printed 3.95124 (|d| = 2.95 = 2.95e9 x tol) and Mean 3.17457.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "processing_enhance_log lowest plateau is ln(0+1) = 0 and it is also the min used by the gate")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 1 -Tolerance 1e-9 -Message (
                "processing_enhance_log must have divided by (maxVal-minVal); an un-normalised product would end at ln(52)")
            # Sandwich, Mean 0.25 + 0.5*ln(11)/ln(52): exact double 0.55343550582843593, magnitude in
            # [0.5,1) -> step 1e-6. THIS point sits 5.83e-9 ABOVE the six-digit rounding midpoint
            # 0.5534355, and the float32 route (cv::log is a table look-up, then a multiply by the
            # reciprocal of ln(52)) perturbs it by ~1e-8, so the printer may legitimately emit 0.553435
            # (|d| = 5.06e-7) or 0.553436 (|d| = 4.94e-7). The tolerance is therefore pinned by the
            # WORST of the two print sides, not chosen: it must exceed 5.06e-7, and 1e-6 is the tightest
            # power of ten above that (1.98x). Smallest distinguishable defect: routing this branch
            # through toUint8 would give ln(51)/ln(256) as the middle level -> Mean 0.604526,
            # |d| = 5.11e-2 = 5.1e4 x the tolerance.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected (0.25 + 0.5 * ([math]::Log(11.0) / [math]::Log(52.0))) -Tolerance 1e-6 -Message (
                "processing_enhance_log mean should be 0.25 + 0.5*ln(11)/ln(52) from the three plateaus 0 / ln11/ln52 / 1")
        }
        "processing_enhance_gamma" {
            Mark-Judged -Name $Case.Name
            Remove-Item ($processingEnhanceGammaOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + $processingEnhanceGammaOutput)
            )
            Assert-InfoSizeExact -Text $info -ExpectedWidth 40 -ExpectedHeight 24 -ExpectedBands 1 -Message (
                "processing_enhance_gamma did not write the 40 x 24 x 1 band product")
            Assert-Condition -Condition ((Get-InfoBandLabelText -Text $info -Band 1 -Label "Type") -eq "Float32") -Message (
                "processing_enhance_gamma product must be Float32 (cv::pow on the normalised CV_32F band)")
            # Sandwich, gamma Min/Max: 0^(1/2) = 0 and 1^(1/2) = 1, both exact in float32 -> zero print
            # residual, 1e-9 for read-back noise only. Max pins that the normalisation ran BEFORE the
            # power: pow on the raw band would stop at 51^(1/2) = 7.14143 (|d| = 6.14, 6.14e9 x tol).
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Min" -Expected 0 -Tolerance 1e-9 -Message (
                "processing_enhance_gamma min plateau stays 0 under any exponent (0^(1/gamma) = 0)")
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Max" -Expected 1 -Tolerance 1e-9 -Message (
                "processing_enhance_gamma max plateau stays 1 because the band is normalised before cv::pow")
            # Sandwich, Mean 0.25 + 0.5*sqrt(10/51) = 0.47140372138502384: magnitude in [0.1,1) -> step
            # 1e-6, half-step 5e-7, so the printed value is 0.471404 and |printed - expected| = 2.79e-7
            # (0.28 x tol). The point is 2.21e-7 ABOVE the rounding midpoint 0.4714035, and cv::pow with
            # a non-integer exponent goes through the float FastPow (log/exp) path, good to a few ulp at
            # magnitude 0.44 where 1 ulp = 5.96e-8 -> <= 3 ulp = 1.8e-7 on the plateau, <= 9e-8 on the
            # Mean. Worst case if that perturbation did flip the print side: 0.471403 -> 7.21e-7, still
            # inside 1e-6 (1.39 x headroom); without a flip the headroom is 2.7 x. Nearest defects:
            # exponent written as gammaVal (x^2) -> 0.26922338, |d| = 0.20218035 = 2.02e5 x tol; gamma
            # not read (default 1.0) -> 0.34803922, |d| = 0.12336451 = 1.23e5 x tol.
            Assert-InfoBandStatNear -Text $info -Band 1 -Label "Mean" -Expected (0.25 + 0.5 * [math]::Sqrt(10.0 / 51.0)) -Tolerance 1e-6 -Message (
                "processing_enhance_gamma mean should be 0.25 + 0.5*sqrt(10/51): the exponent is 1/gamma, so gamma=2.0 brightens")
        }
        "georef_dos_correction" {
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_gcp_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_gcp_register raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "georef_gcp_register crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   7" -Message "georef_gcp_register mean mismatch"
        }
        "georef_cosine_correction" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_cosine_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_cosine_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   20" -Message "georef_cosine_correction mean mismatch"
        }
        "georef_minnaert_correction" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_minnaert_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_minnaert_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   14.1421" -Message "georef_minnaert_correction mean mismatch"
        }
        "georef_c_correction" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_c_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_c_correction raster size mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   18.1239" -Message "georef_c_correction mean mismatch"
        }
        "georef_percentile_stretch" {
            Mark-Judged -Name $Case.Name
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
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "georef_rpc_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   10 x 6 x 1 bands" -Message "georef_rpc_orthorectify raster size mismatch"
            Assert-TextContains -Text $info -Expected "CRS:    EPSG:4326" -Message "georef_rpc_orthorectify crs mismatch"
            Assert-TextContains -Text $info -Expected "Mean:   9" -Message "georef_rpc_orthorectify mean mismatch"
        }
        "classification_feature_stats" {
            Mark-Judged -Name $Case.Name
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "feature_stats_output.json")
            Assert-Condition -Condition ($payload.meta.actual_srs -eq "EPSG:3857") -Message "classification_feature_stats actual_srs mismatch"
            Assert-Condition -Condition ($payload.records.Count -ge 2) -Message "classification_feature_stats should output at least two records"
            $summary = @($payload.records | Where-Object { $_.feature_id -eq "__summary__" })
            Assert-Condition -Condition ($summary.Count -eq 1) -Message "classification_feature_stats summary record missing"
        }
        "classification_feature_stats_csv" {
            Mark-Judged -Name $Case.Name
            $rows = Import-Csv (Join-Path $ResolvedOutputRoot "feature_stats_output.csv")
            Assert-Condition -Condition ($rows.Count -ge 2) -Message "classification_feature_stats_csv should output at least two rows"
            Assert-Condition -Condition ($rows[0].PSObject.Properties.Name -contains "feature_id") -Message "classification_feature_stats_csv missing feature_id column"
            Assert-Condition -Condition ($rows[0].PSObject.Properties.Name -contains "pixel_count") -Message "classification_feature_stats_csv missing pixel_count column"
        }
        "classification_svm_classify" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "classification_svm_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 12 x 1 bands" -Message "classification_svm_classify raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "classification_svm_classify raster type mismatch"
            Assert-TextContains -Text $info -Expected "Min:    1" -Message "classification_svm_classify min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    2" -Message "classification_svm_classify max mismatch"
        }
        "classification_random_forest_classify" {
            Mark-Judged -Name $Case.Name
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "raster_inspect", "info", ("--input=" + (Join-Path $ResolvedOutputRoot "classification_rf_output.tif"))
            )
            Assert-TextContains -Text $info -Expected "Size:   24 x 12 x 1 bands" -Message "classification_random_forest_classify raster size mismatch"
            Assert-TextContains -Text $info -Expected "Type:   Float32" -Message "classification_random_forest_classify raster type mismatch"
            Assert-TextContains -Text $info -Expected "Min:    1" -Message "classification_random_forest_classify min mismatch"
            Assert-TextContains -Text $info -Expected "Max:    2" -Message "classification_random_forest_classify max mismatch"
        }
        "classification_max_likelihood_classify" {
            Mark-Judged -Name $Case.Name
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
    $stepLevelRaster = Join-Path $generatedRoot "step_level_input.tif"
    $histMatchReferenceRaster = Join-Path $generatedRoot "histmatch_reference_input.tif"
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

    # R2 (batch D): the overviews and colormap branches both read width/height, so their input has
    # to be NON-SQUARE -- on a square product "W x H" is an x/y reflexive invariant and an axis swap
    # is an equivalent mutation (nothing turns red). 40 x 24 with three plateau levels 0/10/51 in
    # quadrants; see makeStepLevelRaster in tests/gui_test_data_helper.cpp for the hand-derived
    # multiset {0^240, 10^480, 51^240}.  RETRACTION (batch D fix round 1): the first version of this
    # comment said the plateaus exist because "cv::normalize to 8-bit clips percentiles, so a ramp
    # whose extreme buckets hold only 0.39% of the pixels gets its extremes cut off".  That causal
    # claim was written by this seat into committed text and is WRONG -- controller, reviewer and
    # this seat each disproved it independently.  Measured here: NORM_MINMAX to CV_8U is exactly
    # t = (v - min) * 255 / (max - min) with no histogram and no percentile step, so a 64 x 64
    # i % 256 ramp (every bucket 0.390625% of the pixels) still maps min -> 0 and max -> 255; the
    # product binary on that ramp reads band2 Min 0 / Max 255 / Mean 127.5 / StdDev 73.9003, which
    # equals the independently derived sqrt((256^2 - 1) / 12).  See logs F1_cv2_normalize_probe.log
    # and F1_product_info_ramp64x64.log under build/tmp/batchD/r2/arms/.
    # The REAL reason for plateaus is derivability, not clipping: three levels are directly
    # countable (240/480/240) so both moments are two lines of closed form, while a 256-level ramp
    # needs summation order plus a per-level round-vs-truncate check.  That is a fixture-shape
    # choice made by me, not a product decision.
    if (-not (Test-Path $stepLevelRaster)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("step-level-raster", $stepLevelRaster)
    }

# R3a (batch D): the histogram_match leg branch needs a reference whose pixel multiset is
# countable by hand AND whose distribution differs from the input's. histmatch_reference_input.tif
# is 40 x 24 four-plateau {0, 17, 34, 51} with 240 pixels each (see makeHistMatchReferenceRaster).
# It is generated on demand and only when missing, same as the other fixtures in this directory.
    if (-not (Test-Path $histMatchReferenceRaster)) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("histmatch-reference-raster", $histMatchReferenceRaster)
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
        StepLevelRaster = $stepLevelRaster
        HistMatchReferenceRaster = $histMatchReferenceRaster
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
Copy-Item $data.StepLevelRaster $rasterManageOverviewsInput -Force
# The copy is what the action mutates in place; a sidecar left by an earlier run would answer
# statistics from cache instead of from pixels, so it is dropped before the case runs as well.
Remove-Item ($rasterManageOverviewsInput + ".aux.xml") -Force -ErrorAction SilentlyContinue
$cases += New-Case -Name "raster_manage_overviews" -CaseArgs @(
    "raster_manage", "overviews",
    ("--input=" + $rasterManageOverviewsInput),
    "--levels=2 1 4 0 8",
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

# Non-square, three-plateau input (see makeStepLevelRaster) and cmap=autumn: autumn's green channel
# is the normalised 8-bit level itself, which is what makes the band statistics below derivable from
# the fixture rather than read back out of the product. The output path is bound to a variable
# because the judgement branch has to point at exactly the file this case writes.
$rasterRenderColormapInput = $data.StepLevelRaster
$rasterRenderColormapOutput = Join-Path $ResolvedOutputRoot "colormap_output.tif"
$cases += New-Case -Name "raster_render_colormap" -CaseArgs @(
    "raster_render", "colormap",
    ("--input=" + $rasterRenderColormapInput),
    ("--output=" + $rasterRenderColormapOutput),
    "--band=1",
    "--cmap=autumn"
) -ExpectedOutputs @(
    $rasterRenderColormapOutput
)

# R3a (batch D): inputs for the two branches added by this seat.
# cog writes a NEW file (GDALTranslate output), so the fixture is copied first and the product is
# bound to a variable that the judgement branch points at -- the same shape raster_manage_overviews
# uses, but the copy is only there so an interrupted run cannot leave the shared fixture half-written.
$cogInputRaster = Join-Path $ResolvedOutputRoot "manage_cog_input.tif"
Copy-Item $data.StepLevelRaster $cogInputRaster -Force
Remove-Item ($cogInputRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
$cogOutputRaster = Join-Path $ResolvedOutputRoot "manage_cog_output.tif"
Remove-Item ($cogOutputRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
$cases += New-Case -Name "raster_manage_cog" -CaseArgs @(
    "raster_manage", "cog",
    ("--input=" + $cogInputRaster),
    ("--output=" + $cogOutputRaster)
) -ExpectedOutputs @(
    $cogOutputRaster
)

# histogram_match: source = the 40 x 24 three-plateau fixture {0^240, 10^480, 51^240}; reference =
# a SEPARATE 40 x 24 four-plateau fixture {0^240, 17^240, 34^240, 51^240}. The reference must not
# be the source: with source == reference both CDFs are identical, the LUT becomes the identity and
# "matched" would be indistinguishable from "copied the normalised source" (equivalent mutation).
# Same size on both sides is also deliberate -- the resize path is INTER_LINEAR on the reference and
# its output histogram is then not hand-countable (registered as a gap for this seat).
$histMatchInputRaster = Join-Path $ResolvedOutputRoot "hist_match_input.tif"
Copy-Item $data.StepLevelRaster $histMatchInputRaster -Force
Remove-Item ($histMatchInputRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
$histMatchOutputRaster = Join-Path $ResolvedOutputRoot "hist_match_output.tif"
Remove-Item ($histMatchOutputRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
$cases += New-Case -Name "raster_render_histogram_match" -CaseArgs @(
    "raster_render", "histogram_match",
    ("--input=" + $histMatchInputRaster),
    ("--reference=" + $data.HistMatchReferenceRaster),
    ("--output=" + $histMatchOutputRaster),
    "--band=1"
) -ExpectedOutputs @(
    $histMatchOutputRaster
)

# R3b (batch D): zonal_stats had NO case on this leg at all before this seat -- the action never
# ran here, so a leg-side green for it was impossible by construction. The zones fixture is a
# hand-written GeoJSON rectangle (ASCII literal, written with WriteAllText so no BOM is added);
# the raster is a COPY of makeAnalysisRaster taken into the run's own output root, because opening
# the shared generated fixture in place would let a PAM sidecar answer statistics from cache.
$zonalInputRaster = Join-Path $ResolvedOutputRoot "manage_zonal_input.tif"
Copy-Item $data.AnalysisRaster $zonalInputRaster -Force
Remove-Item ($zonalInputRaster + ".aux.xml") -Force -ErrorAction SilentlyContinue
$zonalVectorPath = Join-Path $ResolvedOutputRoot "manage_zonal_zones.geojson"
[IO.File]::WriteAllText($zonalVectorPath, '{"type":"FeatureCollection","features":[{"type":"Feature","properties":{"zone":"z1"},"geometry":{"type":"Polygon","coordinates":[[[116.0001,39.9989],[116.0019,39.9989],[116.0019,39.9999],[116.0001,39.9999],[116.0001,39.9989]]]}}]}')
$zonalOutputCsv = Join-Path $ResolvedOutputRoot "manage_zonal_output.csv"
Remove-Item $zonalOutputCsv -Force -ErrorAction SilentlyContinue
$cases += New-Case -Name "raster_manage_zonal_stats" -CaseArgs @(
    "raster_manage", "zonal_stats",
    ("--input=" + $zonalInputRaster),
    ("--vector=" + $zonalVectorPath),
    ("--output=" + $zonalOutputCsv),
    "--feature_id_field=zone"
) -ExpectedOutputs @(
    $zonalOutputCsv
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

# D1-C1 (Task 18): the cutting_clip case above lands on a SQUARE 2 x 2 window, and a square is an
# x/y reflexive invariant -- parsing width and height off it cannot see the extent being read in the
# wrong axis order. This twin asks for a 3 x 4 window on the same fixture (extent is given as
# xmin,ymin,xmax,ymax, which is what doClip splices into -projwin ulx uly lrx lry), so width and
# height are different numbers and the branch can tell an axis swap from a correct window.
$cases += New-Case -Name "cutting_clip_asymmetric_window" -CaseArgs @(
    "cutting", "clip",
    ("--input=" + $data.ClassificationRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "cutting_clip_asym_output.tif")),
    "--extent=12935000,4852100,12935150,4852300"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "cutting_clip_asym_output.tif")
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

# D1-C1 (Task 18): the split case above passes overlap=0, so it never exercises the overlap read
# point. This twin keeps tile_size=4 and sets overlap=2 on the same 6 x 6 fixture. doSplit's stride
# is tile_size, so the COUNT stays 4 while tile_0_0 grows to 6 x 6 (min(4+2, 6)) and tile_1_0 /
# tile_0_1 grow to 2 x 6 / 6 x 2 -- those three are the leg-side evidence that overlap is consumed.
# (tile_1_1 stays 2 x 2: the clamp eats the overlap, which is why the last block is not evidence.)
$cases += New-Case -Name "cutting_split_overlap" -CaseArgs @(
    "cutting", "split",
    ("--input=" + $data.ClassificationRaster),
    ("--output=" + (Join-Path $ResolvedOutputRoot "cutting_split_overlap_tiles")),
    "--tile_size=4",
    "--overlap=2"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "cutting_split_overlap_tiles")
)

# D1-C2 (batch D promotion of cutting,tile): tile writes a DIRECTORY tree (z/x/y.png), and the shared
# output root tmp\raster_regression is a fixed path reused across runs -- a stale tree from an earlier
# run would pad the level count and could mask exactly the level-count change this case asserts. One
# run tag per leg invocation keeps both tile trees write-once; the judged branch reads the same two
# script-scope paths, so no name is spelled twice.
$CuttingTileRunTag = [System.Guid]::NewGuid().ToString("N").Substring(0, 8)
$script:CuttingTileRoot = Join-Path $ResolvedOutputRoot ("cutting_tile_" + $CuttingTileRunTag)
$script:CuttingTileTs128Root = Join-Path $ResolvedOutputRoot ("cutting_tile_ts128_" + $CuttingTileRunTag)

# Plugin defaults are what the unit case cannot reach from the CLI, so only the tested key and the
# blank-guard decoupling flag are passed: max_zoom stays unset (its -1 default is the auto band under
# test) and tile_pixel_size keeps its default 256 in this first pass.
$cases += New-Case -Name "cutting_tile" -CaseArgs @(
    "cutting", "tile",
    ("--input=" + $data.ClassificationRaster),
    ("--output=" + $script:CuttingTileRoot),
    "--skip_blank=false"
) -ExpectedOutputs @(
    $script:CuttingTileRoot
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

# P1 (batch D): processing.threshold had NO case on this leg before this seat -- the action never
# ran here, so a leg-side green for it was impossible by construction (grep of this file for
# "processing_threshold" / "threshold_value" returned 0 hits at the start of the seat).
# Input = a COPY of $data.StepLevelRaster under $ResolvedOutputRoot, never the shared original:
# the original is what four other cases read, and an .aux.xml sidecar written against this product
# name would answer band statistics from cache instead of from pixels.
# Why the step-level fixture is the right carrier here: 40 x 24 (non-square, so an axis swap in the
# product size is visible) with plateaus 0 / 10 / 51 whose counts are directly countable (240 / 480 /
# 240 of 960), and cv::normalize maps min->0 and max->255 with scale 255/(51-0) = 5.0, a constant
# that is exactly representable, so 10*5.0 = 50 and 51*5.0 = 255 land off any round-versus-truncate
# boundary. So the normalised levels 0 / 50 / 255 are derived from the fixture, not read back.
# method=binary with threshold_value=30 (below the middle plateau) and max_value=255:
#   u8 > 30 -> the 50 plateau (480 px) AND the 255 plateau (240 px) = 720 px -> max_value/255 = 1.0
#   the 0 plateau (240 px) stays 0.0
#   Mean = 720/960 = 0.75 (exact, prints as "0.75" at six significant digits -> zero rounding)
# Had threshold_value been compared against the RAW pixels instead of the normalised ones, only the
# 51 plateau would qualify (240 px) and Mean would be 0.25 -- this branch is the leg-side witness of
# that one sentence.
$processingThresholdInput = Join-Path $ResolvedOutputRoot "processing_threshold_input.tif"
Copy-Item $data.StepLevelRaster $processingThresholdInput -Force
Remove-Item ($processingThresholdInput + ".aux.xml") -Force -ErrorAction SilentlyContinue
$processingThresholdOutput = Join-Path $ResolvedOutputRoot "processing_threshold_output.tif"
Remove-Item ($processingThresholdOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
$cases += New-Case -Name "processing_threshold" -CaseArgs @(
    "processing", "threshold",
    ("--input=" + $processingThresholdInput),
    ("--output=" + $processingThresholdOutput),
    "--band=1",
    "--method=binary",
    "--threshold_value=30",
    "--max_value=255"
) -ExpectedOutputs @(
    $processingThresholdOutput
)

# P2b (batch D): processing.enhance had NO case on this leg before this seat (grep of this file for
# "processing_enhance" returned 0 hits when the seat started), so no leg-side green for it was possible
# by construction -- P2a promoted the registry row from the UNIT side only.
# One input fixture, four cases (four enhance_type values; clahe is left out, see the why below).
#
# WHY three statistics rows are three judgements here, and why a fourth row is left out:
# the fixture has exactly three distinct raw values (0 x240 / 10 x480 / 51 x240, n = 960) sitting in
# three different x/y quadrants, and every branch of doEnhance maps a whole plateau to one value, so
#   Min = m0,  Max = m2,  Mean = 0.25*m0 + 0.5*m1 + 0.25*m2  =>  m1 = 2*(Mean - 0.25*Min - 0.25*Max)
# i.e. the three printed numbers are a BIJECTION of the three plateau values: each carries one degree
# of freedom and none is a corollary of the others. StdDev is NOT asserted in any of the four branches
# because sigma^2 = 0.25*(m0^2 + m2^2) + 0.5*m1^2 - Mean^2 -- the same three values read a second time
# (this is the reasoning P1 used to drop its own StdDev row, and it applies here a fortiori).
#
# WHY clahe gets no leg case: cv::createCLAHE(clip_limit, cv::Size(8, 8)) passes those numbers as the
# TILE COUNT, so the 40 x 24 input splits into 8 x 8 tiles of 5 x 3 px with tile centres at
# x = 2.5 + 5i, y = 1.5 + 3j; the plateaus split at x = 20 and y = 12, which fall BETWEEN centres, so
# every pixel in the bands around those splits is a bilinear blend of two different per-tile LUTs.
# "raster_inspect info" gives one whole-image Mean for that -- no independently derivable number exists,
# so a clahe branch could only carry a shape judgement. P2a registered exactly that hole as its own gap
# (no expectation for the interpolation region); copying it onto the leg would pad a non-load-bearing
# branch with a Mark-Judged, so this seat ships four cases rather than five.
#
# Input = ONE copy of $data.StepLevelRaster under $ResolvedOutputRoot (shared by the four cases), never
# the original: the original is read by several other cases, and a sidecar keyed to this product name
# would answer band statistics from cache instead of from pixels. The read side itself is not affected
# by any sidecar -- gdalBandToMat is a plain RasterIO and does not consult PAM -- but raster_inspect
# info's statistics ARE cached, so every branch below deletes its own product sidecar before reading.
$processingEnhanceInput = Join-Path $ResolvedOutputRoot "processing_enhance_input.tif"
Copy-Item $data.StepLevelRaster $processingEnhanceInput -Force
Remove-Item ($processingEnhanceInput + ".aux.xml") -Force -ErrorAction SilentlyContinue
$processingEnhanceEqualizeOutput = Join-Path $ResolvedOutputRoot "processing_enhance_equalize_output.tif"
Remove-Item ($processingEnhanceEqualizeOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
$processingEnhanceNormalizeOutput = Join-Path $ResolvedOutputRoot "processing_enhance_normalize_output.tif"
Remove-Item ($processingEnhanceNormalizeOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
$processingEnhanceLogOutput = Join-Path $ResolvedOutputRoot "processing_enhance_log_output.tif"
Remove-Item ($processingEnhanceLogOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue
$processingEnhanceGammaOutput = Join-Path $ResolvedOutputRoot "processing_enhance_gamma_output.tif"
Remove-Item ($processingEnhanceGammaOutput + ".aux.xml") -Force -ErrorAction SilentlyContinue

# equalize: toUint8 first (cv::normalize to CV_8U, scale 255/(51-0) = 5.0, exactly representable, so the
# raw plateaus land on u8 levels 0 / 50 / 255 off any round-versus-truncate boundary), then
# cv::equalizeHist, then convertTo(CV_32F, 1/255). Upstream 4.14.0 histogram.cpp: i0 = first occupied
# level = 0, scale = float32(255/(960-240)) = 0.35416665673255920, lut[0] = 0 is assigned explicitly,
# lut[50] = round(480*scale) and lut[255] = round(720*scale), and those two products evaluate to exactly
# 170.0 and 255.0 IN FLOAT32 (the double view would be 169.99999523 / 254.99999285), so nearest-rounding
# and truncation give the same pair and the expectation does not depend on the rounding mode.
#   plateaus 0 / 170/255 / 1.0  =>  Mean = (240*0 + 480*170/255 + 240*1)/960 = 560/960 = 7/12
# This is a real convention collision, not an arithmetic exercise: the OTHER CDF convention
# (lut = cdf*255/total, "A") would give Min 0.250980 and Mean 0.687255, i.e. |dMean| = 0.104.
$cases += New-Case -Name "processing_enhance_equalize" -CaseArgs @(
    "processing", "enhance",
    ("--input=" + $processingEnhanceInput),
    ("--output=" + $processingEnhanceEqualizeOutput),
    "--band=1",
    "--enhance_type=equalize"
) -ExpectedOutputs @(
    $processingEnhanceEqualizeOutput
)

# normalize: cv::normalize(mat, result, 0, 1, NORM_MINMAX, CV_32F) straight on the raw floats.
#   plateaus 0 / 10/51 / 1.0  =>  Mean = (480*10/51 + 240*1)/960 = 71/204
# WHAT THIS BRANCH CANNOT JUDGE (leg-side gap, do not read it as covering DF-4): on THIS fixture the
# "is normalize quantised to 8 bits or not" question has no observable. toUint8's scale is exactly
# 255/(51-0) = 5.0, so the raw plateaus land on u8 levels 0 / 50 / 255 and 50/255 = 10/51 is the SAME
# RATIONAL as the un-quantised middle level -- the two candidate implementations print identical
# Min / Mean / Max here. The discriminator exists only on a fixture whose max is not a divisor of 255
# (the unit side uses max = 53, where 10*255/53 truncates to 48 and 48/255 differs from 10/53 by
# 7.84e-4). So this case pins that enhance_type=normalize reaches the CLI, reads the band, runs the
# min/max normalisation and round-trips through GDAL's statistics; the "not quantised" half of DF-4
# stays unit-side only and is registered as a leg-side gap in the seat report.
$cases += New-Case -Name "processing_enhance_normalize" -CaseArgs @(
    "processing", "enhance",
    ("--input=" + $processingEnhanceInput),
    ("--output=" + $processingEnhanceNormalizeOutput),
    "--band=1",
    "--enhance_type=normalize"
) -ExpectedOutputs @(
    $processingEnhanceNormalizeOutput
)

# log: cv::max(mat, 0.0, clamped) then +1.0 then cv::log, then min/max normalisation behind a
# (maxVal - minVal) > 1e-10 gate. All three plateaus are non-negative here, so the clamp is the identity
# and the levels are ln(1)=0 / ln(11) / ln(52); 0 is the min and ln(52) the max, so after normalising
#   plateaus 0 / ln(11)/ln(52) / 1.0  =>  Mean = 0.25 + 0.5*ln(11)/ln(52) = 0.55343550582843593
# Max = 1 is what makes the 1e-10 gate observable: gate removed / always-normalise on a constant band is
# a different branch, and here a dropped normalisation would leave Max at ln(52) = 3.95124.
# Had this branch been routed through toUint8, the middle level would be ln(51)/ln(256) = 0.709053 and
# the Mean 0.604526 -- delta 5.109e-2 from the expectation below.
$cases += New-Case -Name "processing_enhance_log" -CaseArgs @(
    "processing", "enhance",
    ("--input=" + $processingEnhanceInput),
    ("--output=" + $processingEnhanceLogOutput),
    "--band=1",
    "--enhance_type=log"
) -ExpectedOutputs @(
    $processingEnhanceLogOutput
)

# gamma: the constant branch normalises first and then applies pow(., 1.0/gammaVal) -- the exponent is
# RECIPROCAL, which is what makes gamma > 1 BRIGHTEN (the shipped algorithm doc said the opposite until
# P2a corrected it). With --gamma=2.0 on this fixture:
#   plateaus 0 / sqrt(10/51) / 1.0  =>  Mean = 0.25 + 0.5*sqrt(10/51) = 0.47140372138502384
# The two nearest mis-implementations are both far outside the tolerance used below: exponent written as
# gammaVal itself (x^2) gives Mean 0.26922337562475973 (delta 0.20218035), and gamma silently ignored
# (staying at the ParamSpec default 1.0, where pow(x,1) collapses onto the normalize branch) gives
# 0.34803921568627449 (delta 0.12336451). Min 0 / Max 1 also pin that the normalisation ran BEFORE the
# power: skipping it would put Max at sqrt(51) = 7.14143.
$cases += New-Case -Name "processing_enhance_gamma" -CaseArgs @(
    "processing", "enhance",
    ("--input=" + $processingEnhanceInput),
    ("--output=" + $processingEnhanceGammaOutput),
    "--band=1",
    "--enhance_type=gamma",
    "--gamma=2.0"
) -ExpectedOutputs @(
    $processingEnhanceGammaOutput
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
    Note-CaseRun -Name $case.Name
    $results += Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
    Validate-CaseOutputs -Case $case -ResolvedOutputRoot $ResolvedOutputRoot -ResolvedCliPath $ResolvedCliPath
}

# Allowlist for the shared ran-must-be-judged gate (tests/regression_case_gate.ps1).
# One entry per case that this leg runs but does not judge, each with the reason for the
# missing judgement. The gate asserts the reverse necessity in BOTH directions: the moment
# a branch is added for one of these names the stale entry turns the leg red, and an entry whose
# case is neither registered in this run nor backed by a gated registration site in this file
# turns the leg red too. So this list can neither rot forward (a branch appears and the entry is
# forgotten) nor be orphaned backwards (someone deletes the New-Case the entry excuses).
# The gate's printed "allowlisted=" is the number of entries this leg actually consulted for a
# case that ran, not the size of the table below. On this leg the two agree (re-measured 21/21 on
# the Debug leg by R2, after raster_manage_overviews and raster_render_colormap moved out of this
# table into their own value-level judgement branches; R1 had measured 23/23 when
# raster_inspect_info moved), because this file has exactly one gated registration site (the gate
# prints it as conditional-registrations=1) and that name is not one of the entries below; the two
# counters are printed separately so a leg where they differ cannot be read as a coverage number.
$rasterGateAllowlist = @(
    [pscustomobject]@{ Name = "spindex_evi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_savi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_gndvi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_ndwi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_mndwi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_ndbi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_arvi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_nbr"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_awei"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_ui"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_bi"; Reason = "fixture-side GOLD in tests/action_gold_cases.txt via PluginTest.SpindexOtherIndicesExecution; this leg still has no independently derived expectation for its generated 24x24 input" },
    [pscustomobject]@{ Name = "spindex_custom_index"; Reason = "fixture-side GOLD via PluginTest.SpindexCustomIndexExecution; no leg-side expectation derived" },
    [pscustomobject]@{ Name = "raster_manage_nodata"; Reason = "fixture-side GOLD via PluginTest.RasterManageNoDataExecution; leg only checks the file exists" },
    [pscustomobject]@{ Name = "terrain_fill_sinks"; Reason = "fixture-side GOLD via PluginTest.TerrainFillSinksAndFlowDirectionExecution; leg-side DEM has no hand-derived sink expectation" },
    [pscustomobject]@{ Name = "terrain_flow_direction"; Reason = "fixture-side GOLD via PluginTest.TerrainFillSinksAndFlowDirectionExecution; leg-side DEM has no hand-derived coding expectation" },
    [pscustomobject]@{ Name = "terrain_flow_accumulation"; Reason = "fixture-side GOLD via PluginTest.TerrainFlowAccumulationExecution; leg-side accumulation total was not derived independently" },
    [pscustomobject]@{ Name = "terrain_stream_extract"; Reason = "fixture-side GOLD via PluginTest.TerrainStreamExtractExecution; leg-side threshold-to-count mapping not derived" },
    [pscustomobject]@{ Name = "terrain_watershed"; Reason = "fixture-side GOLD via PluginTest.TerrainWatershedExecution; watershed rewrites non-zero pixels, leg-side labelling not derived" },
    [pscustomobject]@{ Name = "terrain_viewshed"; Reason = "fixture-side GOLD via PluginTest.TerrainViewshedExecution; visibility set not derived for this DEM/observer pair" },
    [pscustomobject]@{ Name = "terrain_cut_fill"; Reason = "fixture-side GOLD via PluginTest.TerrainCutFillExecution; cut/fill volumes not derived for this DEM" },
    [pscustomobject]@{ Name = "terrain_reservoir_volume"; Reason = "fixture-side GOLD via PluginTest.TerrainReservoirVolumeExecution; reservoir volume not derived for this DEM" }
)
Assert-AllCasesJudged -RunnerName "raster" -RunnerPath $PSCommandPath -SelectedGroup "all" -Allowlist $rasterGateAllowlist
Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Raster regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(28) + " " + $result.Seconds + "s")
}
