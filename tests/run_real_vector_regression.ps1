param(
    [string]$CliPath = "",
    [string]$WorkspaceRoot = "",
    [string]$OutputRoot = "",
    [ValidateSet("quick", "full")]
    [string]$Mode = "quick"
)

$ErrorActionPreference = "Stop"

function Get-DefaultWorkspaceRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-DefaultCliPath {
    param([string]$Root)
    return Join-Path $Root "build-vs2022-global\src\cli\Debug\gis-cli.exe"
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

    $buildRoot = Resolve-Path (Join-Path $cliDir "..\..")
    $pluginDlls = Get-ChildItem (Join-Path $buildRoot "plugins") -Recurse -Filter "plugin_*.dll" -ErrorAction SilentlyContinue
    foreach ($dll in $pluginDlls) {
        Copy-Item $dll.FullName $pluginDir -Force
    }
}

function New-Case {
    param(
        [string]$Name,
        [string[]]$CaseArgs
    )

    return [pscustomobject]@{
        Name = $Name
        Args = [string[]]$CaseArgs
    }
}

function Invoke-Case {
    param(
        [string]$ResolvedCliPath,
        [pscustomobject]$Case
    )

    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $arguments = [string[]]$Case.Args
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & $ResolvedCliPath @arguments 2>&1
    $ErrorActionPreference = $previousErrorActionPreference
    $code = $LASTEXITCODE
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

$benchmarkRoot = Join-Path $ResolvedWorkspaceRoot "tmp\vector_benchmark"
$roadsCore = Join-Path $benchmarkRoot "bj_roads_core.shp"
$roadsCore3857 = Join-Path $benchmarkRoot "bj_roads_core_3857.gpkg"
$chinaFocus = Join-Path $benchmarkRoot "china_focus.geojson"
$chinaBbox = Join-Path $benchmarkRoot "china_bbox.geojson"
$chinaBbox3857 = Join-Path $benchmarkRoot "china_bbox_3857.gpkg"

Require-Path -Path $roadsCore -Label "roads core"
Require-Path -Path $roadsCore3857 -Label "roads core 3857"
Require-Path -Path $chinaFocus -Label "china focus"
Require-Path -Path $chinaBbox -Label "china bbox"
Require-Path -Path $chinaBbox3857 -Label "china bbox 3857"

$cases = @()
$cases += New-Case -Name "buffer_quick" -CaseArgs @(
    "vector", "buffer",
    ("--input=" + $roadsCore3857),
    ("--output=" + (Join-Path $ResolvedOutputRoot "buffer_quick.gpkg")),
    "--distance=100"
)
$cases += New-Case -Name "clip_focus" -CaseArgs @(
    "vector", "clip",
    ("--input=" + $roadsCore),
    ("--clip_vector=" + $chinaFocus),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_focus.gpkg"))
)
$cases += New-Case -Name "difference_focus" -CaseArgs @(
    "vector", "difference",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $chinaFocus),
    ("--output=" + (Join-Path $ResolvedOutputRoot "difference_focus.gpkg"))
)
$cases += New-Case -Name "union_focus" -CaseArgs @(
    "vector", "union",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $chinaFocus),
    ("--output=" + (Join-Path $ResolvedOutputRoot "union_focus.gpkg"))
)
$cases += New-Case -Name "dissolve_bbox" -CaseArgs @(
    "vector", "dissolve",
    ("--input=" + $chinaBbox3857),
    ("--output=" + (Join-Path $ResolvedOutputRoot "dissolve_bbox.gpkg"))
)

if ($Mode -eq "full") {
    $cases += New-Case -Name "clip_stress" -CaseArgs @(
        "vector", "clip",
        ("--input=" + $roadsCore),
        ("--clip_vector=" + $chinaBbox),
        ("--output=" + (Join-Path $ResolvedOutputRoot "clip_stress.gpkg"))
    )
    $cases += New-Case -Name "difference_stress" -CaseArgs @(
        "vector", "difference",
        ("--input=" + $roadsCore),
        ("--overlay_vector=" + $chinaBbox),
        ("--output=" + (Join-Path $ResolvedOutputRoot "difference_stress.gpkg"))
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
