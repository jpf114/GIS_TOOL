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
    return Join-Path $Root "tmp\matching_regression"
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
        if (-not $item.PSIsContainer -and $item.Length -le 0) {
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

function Get-TiffDimensions {
    param([string]$Path)

    # Minimal baseline-TIFF IFD0 reader: tags 256/257 only.
    # GDAL writes little-endian by default; anything else is a hard error here
    # so the criterion can never silently vacuate.
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    Assert-Condition -Condition ($bytes.Length -ge 8) -Message ("TIFF too short: " + $Path)
    Assert-Condition -Condition ($bytes[0] -eq 0x49 -and $bytes[1] -eq 0x49) `
        -Message ("Get-TiffDimensions expects little-endian II TIFF: " + $Path)
    $ifdOffset = [BitConverter]::ToInt32($bytes, 4)
    $entryCount = [BitConverter]::ToUInt16($bytes, $ifdOffset)
    $width = -1
    $height = -1
    for ($i = 0; $i -lt $entryCount; $i++) {
        $entryStart = $ifdOffset + 2 + ($i * 12)
        $tag = [BitConverter]::ToUInt16($bytes, $entryStart)
        if ($tag -eq 256 -or $tag -eq 257) {
            $value = [BitConverter]::ToInt32($bytes, $entryStart + 8)
            if ($tag -eq 256) { $width = $value } else { $height = $value }
        }
    }
    Assert-Condition -Condition ($width -gt 0 -and $height -gt 0) `
        -Message ("TIFF width/height tags 256/257 not found: " + $Path)
    return [pscustomobject]@{ Width = $width; Height = $height }
}

function Validate-CaseOutputs {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedOutputRoot,
        [string]$ReferencePath
    )

    switch ($Case.Name) {
        "matching_detect" {
            Mark-Judged -Name $Case.Name
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "detect_output.json")
            # Value-level (library contract): cv::ORB::create(nTopFeatures) never exceeds the cap.
            # Case asks max_points=20; uncap ORB::create() and this assert goes red (batchD mutation M1).
            Assert-Condition -Condition ($payload.count -gt 0) -Message "matching_detect count should be greater than zero"
            Assert-Condition -Condition ($payload.count -le 20) -Message ("matching_detect count exceeds max_points cap: " + $payload.count)
            Assert-Condition -Condition ($payload.keypoints.Count -eq $payload.count) -Message "matching_detect keypoint count mismatch"
        }
        "matching_corner" {
            Mark-Judged -Name $Case.Name
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "corner_output.json")
            # Value-level (cap ladder, D1-M): both branches truncate exactly at max_corners when
            # candidates are sufficient; case passes max_corners=3 so count must be exactly 3.
            Assert-Condition -Condition ($payload.count -eq 3) -Message ("matching_corner count should equal max_corners=3, got " + $payload.count)
            Assert-Condition -Condition ($payload.keypoints.Count -eq $payload.count) -Message "matching_corner keypoint count mismatch"
        }
        "matching_match" {
            Mark-Judged -Name $Case.Name
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "match_output.json")
            Assert-Condition -Condition ($payload.count -gt 0) -Message "matching_match count should be greater than zero"
            Assert-Condition -Condition ($payload.matches.Count -eq $payload.count) -Message "matching_match match count mismatch"
            # Predicate-side fact behind the ratio-zero criterion: distances are non-negative.
            # NOTE: the ratio_test=0 value assert cannot live in this leg -- CLI validation
            # (action_validation.cpp:574) restricts matching ratio_test to (0,1], rc=1 rejects 0.
            # The zero point is pinned C++-side only (PluginTest.MatchingMatchRatioTestLadderAndZeroPoint).
            foreach ($m in @($payload.matches)) {
                Assert-Condition -Condition ($m.distance -ge 0) -Message ("matching_match negative distance in payload: " + $m.distance)
                Assert-Condition -Condition ($null -ne $m.idx1 -and $null -ne $m.idx2) -Message "matching_match match entry missing idx1/idx2"
            }
        }
        "matching_detect_sift" {
            Mark-Judged -Name $Case.Name
            $payload = Read-JsonPayload -Path (Join-Path $ResolvedOutputRoot "detect_sift_output.json")
            Assert-Condition -Condition ($payload.count -gt 0) -Message "matching_detect_sift count should be greater than zero"
            Assert-Condition -Condition ($payload.keypoints.Count -eq $payload.count) -Message "matching_detect_sift keypoint count mismatch"
        }
        "matching_register" {
            Mark-Judged -Name $Case.Name
            # Criterion branch added by D1-M (case existed, validation did not).
            # Warp target size is decided by the reference raster (doRegister warps to refMat.size()),
            # so output dims must equal reference dims for any fixture. No content-count constants.
            $refDims = Get-TiffDimensions -Path $ReferencePath
            $outDims = Get-TiffDimensions -Path (Join-Path $ResolvedOutputRoot "register_output.tif")
            Assert-Condition -Condition ($outDims.Width -eq $refDims.Width) `
                -Message ("matching_register width " + $outDims.Width + " != reference width " + $refDims.Width)
            Assert-Condition -Condition ($outDims.Height -eq $refDims.Height) `
                -Message ("matching_register height " + $outDims.Height + " != reference height " + $refDims.Height)
        }
    }
}

function Test-DebugCliBuild {
    param([string]$ResolvedCliPath)

    $cliDirName = Split-Path (Split-Path $ResolvedCliPath -Parent) -Leaf
    return $cliDirName -ieq "Debug"
}

function Ensure-MatchingRegressionData {
    param(
        [string]$ResolvedWorkspaceRoot,
        [string]$ResolvedHelperPath
    )

    $benchmarkRoot = Join-Path $ResolvedWorkspaceRoot "tmp\matching_benchmark"
    $generatedRoot = Join-Path $benchmarkRoot "generated_data"
    New-Item -ItemType Directory -Force -Path $benchmarkRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $generatedRoot | Out-Null

    $matchingDataRoot = Join-Path $ResolvedWorkspaceRoot "data\matching"
    $reference = Join-Path $matchingDataRoot "reference.tif"
    $input = Join-Path $matchingDataRoot "input.tif"
    $changed = Join-Path $matchingDataRoot "change_input.tif"
    $stitchFirst = Join-Path $generatedRoot "stitch_first.tif"
    $stitchSecond = Join-Path $generatedRoot "stitch_second.tif"

    if ((-not (Test-Path $reference)) -or (-not (Test-Path $input)) -or (-not (Test-Path $changed))) {
        $reference = Join-Path $generatedRoot "reference.tif"
        $input = $reference
        $changed = Join-Path $generatedRoot "change_input.tif"
        if ((-not (Test-Path $reference)) -or (-not (Test-Path $changed))) {
            Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("matching-inputs", $reference, $changed)
        }
    }

    if ((-not (Test-Path $stitchFirst)) -or (-not (Test-Path $stitchSecond))) {
        Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("matching-stitch-inputs", $stitchFirst, $stitchSecond)
    }

    return [pscustomobject]@{
        Reference = $reference
        Input = $input
        Changed = $changed
        StitchFirst = $stitchFirst
        StitchSecond = $stitchSecond
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
$data = Ensure-MatchingRegressionData -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -ResolvedHelperPath $ResolvedHelperPath
$isDebugCliBuild = Test-DebugCliBuild -ResolvedCliPath $ResolvedCliPath

$cases = @()
$cases += New-Case -Name "matching_detect" -CaseArgs @(
    "matching", "detect",
    ("--input=" + $data.Reference),
    ("--output=" + (Join-Path $ResolvedOutputRoot "detect_output.json")),
    "--method=orb",
    "--max_points=20"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "detect_output.json")
)

$cases += New-Case -Name "matching_corner" -CaseArgs @(
    "matching", "corner",
    ("--input=" + $data.Reference),
    ("--output=" + (Join-Path $ResolvedOutputRoot "corner_output.json")),
    "--corner_method=shi_tomasi",
    "--max_corners=3"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "corner_output.json")
)

$cases += New-Case -Name "matching_match" -CaseArgs @(
    "matching", "match",
    ("--reference=" + $data.Reference),
    ("--input=" + $data.Input),
    ("--output=" + (Join-Path $ResolvedOutputRoot "match_output.json")),
    "--method=orb",
    "--match_method=bf",
    "--max_points=200",
    "--ratio_test=0.95"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "match_output.json")
)

$cases += New-Case -Name "matching_register" -CaseArgs @(
    "matching", "register",
    ("--reference=" + $data.Reference),
    ("--input=" + $data.Input),
    ("--output=" + (Join-Path $ResolvedOutputRoot "register_output.tif")),
    "--method=orb",
    "--match_method=bf",
    "--transform=affine",
    "--max_points=200",
    "--ratio_test=0.95"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "register_output.tif")
)

$cases += New-Case -Name "matching_change" -CaseArgs @(
    "matching", "change",
    ("--reference=" + $data.Reference),
    ("--input=" + $data.Changed),
    ("--output=" + (Join-Path $ResolvedOutputRoot "change_output.tif")),
    "--change_method=differencing",
    "--threshold=1.0"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "change_output.tif")
)

if (-not $isDebugCliBuild) {
    $cases += New-Case -Name "matching_ecc_register" -CaseArgs @(
        "matching", "ecc_register",
        ("--reference=" + $data.Reference),
        ("--input=" + $data.Input),
        ("--output=" + (Join-Path $ResolvedOutputRoot "ecc_register_output.tif")),
        "--ecc_motion=affine",
        "--ecc_iterations=100",
        "--ecc_epsilon=1e-5"
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "ecc_register_output.tif")
    )

    $cases += New-Case -Name "matching_stitch" -CaseArgs @(
        "matching", "stitch",
        ("--input=" + $data.StitchFirst + "," + $data.StitchSecond),
        ("--output=" + (Join-Path $ResolvedOutputRoot "stitch_output.tif")),
        "--stitch_confidence=0.3"
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "stitch_output.tif")
    )
}

if ($Mode -eq "full") {
    $cases += New-Case -Name "matching_detect_sift" -CaseArgs @(
        "matching", "detect",
        ("--input=" + $data.Reference),
        ("--output=" + (Join-Path $ResolvedOutputRoot "detect_sift_output.json")),
        "--method=sift",
        "--max_points=200"
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "detect_sift_output.json")
    )
}

$results = @()
foreach ($case in $cases) {
    Note-CaseRun -Name $case.Name
    $results += Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
    Validate-CaseOutputs -Case $case -ResolvedOutputRoot $ResolvedOutputRoot -ReferencePath $data.Reference
}

# Allowlist for the shared ran-must-be-judged gate (tests/regression_case_gate.ps1).
# Each entry states why this leg runs the case without judging it; the gate also fails on a
# stale entry, so adding a branch here must be paired with deleting the entry.
$matchingGateAllowlist = @(
    [pscustomobject]@{ Name = "matching_change"; Reason = "fixture-side GOLD via PluginTest.MatchingChangeExecutionReportsChangedPixels; the leg has no independently derived changed-pixel count for its generated pair" }
    [pscustomobject]@{ Name = "matching_ecc_register"; Reason = "PENDING in tests/action_gold_cases.txt; the case only registers on a non-Debug CLI build (R-D25 lightweight gate), and no judgement branch exists for it yet" }
    [pscustomobject]@{ Name = "matching_stitch"; Reason = "PENDING in tests/action_gold_cases.txt; the case only registers on a non-Debug CLI build (R-D25 lightweight gate), and no judgement branch exists for it yet" }
)
Assert-AllCasesJudged -RunnerName "matching" -RunnerPath $PSCommandPath -SelectedGroup "all" -Allowlist $matchingGateAllowlist
Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Matching regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(22) + " " + $result.Seconds + "s")
}
