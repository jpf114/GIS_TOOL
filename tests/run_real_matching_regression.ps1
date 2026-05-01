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

    if ((-not (Test-Path $reference)) -or (-not (Test-Path $input)) -or (-not (Test-Path $changed))) {
        $reference = Join-Path $generatedRoot "reference.tif"
        $input = $reference
        $changed = Join-Path $generatedRoot "change_input.tif"
        if ((-not (Test-Path $reference)) -or (-not (Test-Path $changed))) {
            Invoke-Helper -ResolvedHelperPath $ResolvedHelperPath -Arguments @("matching-inputs", $reference, $changed)
        }
    }

    return [pscustomobject]@{
        Reference = $reference
        Input = $input
        Changed = $changed
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

$cases = @()
$cases += New-Case -Name "matching_detect" -CaseArgs @(
    "matching", "detect",
    ("--input=" + $data.Reference),
    ("--output=" + (Join-Path $ResolvedOutputRoot "detect_output.json")),
    "--method=orb",
    "--max_points=200"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "detect_output.json")
)

$cases += New-Case -Name "matching_corner" -CaseArgs @(
    "matching", "corner",
    ("--input=" + $data.Reference),
    ("--output=" + (Join-Path $ResolvedOutputRoot "corner_output.json")),
    "--corner_method=shi_tomasi",
    "--max_corners=50"
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
    $results += Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
}

Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Matching regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(22) + " " + $result.Seconds + "s")
}
