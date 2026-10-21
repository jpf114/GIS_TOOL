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

    # 数字一律抓整串再比相等，抓不到就判红 —— 禁止用 Contains 判数字：
    # 本文件写过 "feature_count: 2" 这种判据，被真实值 22836 当**前缀**吃掉，假了很多轮。
    # 坐标必须带可选指数段并按 ^…$ 整行锚定：vector info 用 %g 打印坐标，3857 的度量坐标被打成
    # "1.28483e+07"。不带指数段的正则在 "1.28483" 处就对不上后面的逗号 ⇒ 整条匹配失败（好），
    # 但"匹配上却把 e+07 截掉"这种更坏的形态也必须被锚点挡住，所以这里用锚而不只是放宽。
    $number = "-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?"
    $count = [regex]::Match($text, ("(?m)^\s*Features:\s+(\d+)\s*$"))
    Assert-Condition -Condition ($count.Success) -Message ("layer stats: no Features line in info output for " + $Path)
    $extent = [regex]::Match($text, ("(?m)^\s*Extent:\s+(" + $number + "),\s*(" + $number + ")\s+-\s+(" + $number + "),\s*(" + $number + ")\s*$"))
    Assert-Condition -Condition ($extent.Success) -Message ("layer stats: no 'Extent: minx, miny - maxx, maxy' line in info output for " + $Path)
    # 图层几何类型也现读回来：派生腿要用它钉住"我喂的确实是线图层"，不许按 input_kind 猜。
    $geometry = [regex]::Match($text, ("(?m)^\s*Geometry:\s+(\S+)\s*$"))
    Assert-Condition -Condition ($geometry.Success) -Message ("layer stats: no Geometry line in info output for " + $Path)

    return [pscustomobject]@{
        FeatureCount = [int]$count.Groups[1].Value
        Geometry = $geometry.Groups[1].Value
        MinX = [double]$extent.Groups[1].Value
        MinY = [double]$extent.Groups[2].Value
        MaxX = [double]$extent.Groups[3].Value
        MaxY = [double]$extent.Groups[4].Value
    }
}

function Format-Extent {
    param([object]$Stats)

    return ("[" + $Stats.MinX + ", " + $Stats.MinY + " - " + $Stats.MaxX + ", " + $Stats.MaxY + "]")
}

function Assert-ExtentEqualTo {
    param(
        [object]$Expected,
        [object]$Actual,
        [double]$Tolerance,
        [string]$Label
    )

    # 凸包与外包框保范围：四个边界各自与输入对撞，取最大偏差报出来。
    $offsets = @(
        [Math]::Abs($Actual.MinX - $Expected.MinX),
        [Math]::Abs($Actual.MinY - $Expected.MinY),
        [Math]::Abs($Actual.MaxX - $Expected.MaxX),
        [Math]::Abs($Actual.MaxY - $Expected.MaxY)
    )
    $worst = ($offsets | Measure-Object -Maximum).Maximum
    Assert-Condition -Condition ($worst -le $Tolerance) -Message (
        $Label + ": extent not equal to input extent, worst boundary offset " + $worst +
        " > tolerance " + $Tolerance + "; input " + (Format-Extent $Expected) +
        ", output " + (Format-Extent $Actual))
}

function Assert-ExtentInsideOf {
    param(
        [object]$Outer,
        [object]$Inner,
        [double]$Tolerance,
        [string]$Label
    )

    # 图层级几何包含：派生点/线只会落在输入自己的范围里。容差把**外框**往外扩（不是把内框往外拉）：
    # 违规是 inner.min < outer.min - tol 与 inner.max > outer.max + tol。
    # ⚠ 这里写反过一次（写成 inner.min - tol < outer.min），那条错判据会把"内外完全相等"也判红 ——
    # boundary 的输出 Extent 与输入逐字符相同，于是当场红在跑动里，不是红在推理里。
    # 逐边报出是哪一条越界、越了多少，红的时候不用重跑就知道方向。
    $violations = @()
    if ($Inner.MinX -lt ($Outer.MinX - $Tolerance)) { $violations += ("minx " + $Inner.MinX + " < " + $Outer.MinX) }
    if ($Inner.MinY -lt ($Outer.MinY - $Tolerance)) { $violations += ("miny " + $Inner.MinY + " < " + $Outer.MinY) }
    if ($Inner.MaxX -gt ($Outer.MaxX + $Tolerance)) { $violations += ("maxx " + $Inner.MaxX + " > " + $Outer.MaxX) }
    if ($Inner.MaxY -gt ($Outer.MaxY + $Tolerance)) { $violations += ("maxy " + $Inner.MaxY + " > " + $Outer.MaxY) }
    Assert-Condition -Condition ($violations.Count -eq 0) -Message (
        $Label + ": output extent not contained in input extent (" + ($violations -join "; ") +
        "); input " + (Format-Extent $Outer) + ", output " + (Format-Extent $Inner))
}

function Get-ReportedMetadataCount {
    param(
        [string]$Text,
        [string]$Key,
        [string]$Label
    )

    # 整行锚定（^…$）+ 抓完整数字串：键前不许有别的单词字符、行尾不许有尾巴，
    # 于是 "source_count" 不会被 "feature_count" 的判据顺带满足，短数字也不会被长数字当前缀吃。
    return Get-ReportedCount -Text $Text -Pattern ("(?m)^\s*" + $Key + ":\s+(\d+)\s*$") -Label $Label
}

# ── C3a：几何派生批（convex_hull / centroid / envelope / boundary / 三个 extract / interior_point）──
# 每条用例的判据都是"与数据无关的关系"，全部由输入图层自己的现读属性推出来：
#   输出可被 vector info 读回（读不回即红，Features 行缺失即红，解析失败即红，不许静默归零）；
#   输出 Geometry 类型必须是该动作定义的产物类型（centroid/interior_point/三个 extract ⇒ Point，
#     convex_hull/envelope ⇒ Polygon，boundary ⇒ LineString/MultiLineString）；
#   envelope 与 convex_hull ⇒ 输出 Extent 与输入 Extent 相等（保范围，直接打死"只取第一个要素"
#     与"聚合方向写反"）；其余 ⇒ 输出 Extent 被输入 Extent 包含（外扩 1e-6 吸收 %g 打印舍入）；
#   逐要素派生的动作 ⇒ 输出 Features == 输入 Features；三个 extract 是"把几何炸开成点"，
#     要素数按定义必然大于输入（实测 800 要素的线切片 ⇒ 6655 顶点 / 1600 端点 / 800 中点），
#     所以它们之间核关系：vertices >= 2*midpoints、endpoints == 2*midpoints、vertices >= endpoints，
#     这三条一起打死"中点按段数算""端点漏一半""只处理第一个要素"。
#   每个动作另外核"CLI 自报的 feature_count == vector info 现读回来的 Features"（报数与落盘对撞）。
$script:DerivedExtentTolerance = 1e-6

function Assert-DerivedGeometryCase {
    param(
        [string]$Name,
        [string]$ResolvedCliPath,
        [string]$OutputPath,
        [string]$ScratchDir,
        [string]$CliOutput,
        [object]$InputStats,
        [string]$ExpectedGeometryPattern,
        [ValidateSet("equal", "inside", "none")]
        [string]$ExtentMode,
        [ValidateSet("equal", "none")]
        [string]$FeaturesMode
    )

    $read = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $OutputPath -ScratchDir $ScratchDir

    Assert-Condition -Condition ($read.FeatureCount -gt 0) -Message (
        $Name + ": output layer has no feature (Features: " + $read.FeatureCount + ")")
    # 允许同一坐标类的 Multi* 形态（boundary 在多边形输入上产出 MultiLineString 而不是 LineString），
    # 但不允许跨类： centroid 变成 Polygon、vertices 变成 MultiPolygon 这种"派生成了别的东西"必须红。
    Assert-Condition -Condition ($read.Geometry -match $ExpectedGeometryPattern) -Message (
        $Name + ": output geometry type is " + $read.Geometry + ", expected pattern " + $ExpectedGeometryPattern)

    if ($ExtentMode -eq "equal") {
        Assert-ExtentEqualTo -Expected $InputStats -Actual $read -Tolerance $script:DerivedExtentTolerance -Label $Name
    } elseif ($ExtentMode -eq "inside") {
        Assert-ExtentInsideOf -Outer $InputStats -Inner $read -Tolerance $script:DerivedExtentTolerance -Label $Name
    }

    if ($FeaturesMode -eq "equal") {
        Assert-Condition -Condition ($read.FeatureCount -eq $InputStats.FeatureCount) -Message (
            $Name + ": output Features " + $read.FeatureCount + " != input Features " + $InputStats.FeatureCount +
            " (per-feature derivation must not add or drop features)")
    }

    $reported = Get-ReportedMetadataCount -Text $CliOutput -Key "feature_count" -Label ($Name + " reported feature_count")
    Assert-Condition -Condition ($reported -eq $read.FeatureCount) -Message (
        $Name + ": CLI reported feature_count " + $reported + " but vector info reads back " +
        $read.FeatureCount + " (自报数与落盘数不一致)")

    return $read
}

function Validate-DerivedGeometryRelations {
    param(
        [string]$Name,
        [int]$VertexCount,
        [int]$EndpointCount,
        [int]$MidpointCount,
        [int]$InputFeatureCount
    )

    # 三个 extract 只在彼此之间核关系，不核绝对数：绝对数会随 data/vector 换文件而漂移，
    # 而"每段 2 个端点 / 1 个中点 / >=2 个顶点"是这些动作自己的定义。
    if ($Name -eq "midpoints_line") {
        Assert-Condition -Condition ($VertexCount -ge 0) -Message "midpoints_line ran before vertices_line, cannot cross-check"
        Assert-Condition -Condition ($VertexCount -ge 2 * $MidpointCount) -Message (
            "midpoints_line: vertices " + $VertexCount + " < 2 * midpoints " + $MidpointCount +
            " (每条线至少 2 个顶点却只有 1 个中点)")
        Assert-Condition -Condition ($MidpointCount -ge $InputFeatureCount) -Message (
            "midpoints_line: midpoints " + $MidpointCount + " < input features " + $InputFeatureCount +
            " (每个线要素至少产出 1 个中点)")
    }
    if ($Name -eq "endpoints_line") {
        Assert-Condition -Condition ($MidpointCount -ge 0) -Message "endpoints_line ran before midpoints_line, cannot cross-check"
        Assert-Condition -Condition ($EndpointCount -eq 2 * $MidpointCount) -Message (
            "endpoints_line: endpoints " + $EndpointCount + " != 2 * midpoints " + $MidpointCount +
            " (每条线首尾各 1 个端点)")
        Assert-Condition -Condition ($VertexCount -ge $EndpointCount) -Message (
            "endpoints_line: vertices " + $VertexCount + " < endpoints " + $EndpointCount +
            " (端点是顶点的子集)")
    }
}

function Ensure-DerivedGeometryInputs {
    param(
        [string]$ResolvedCliPath,
        [string]$ResolvedWorkspaceRoot,
        [string]$RoadsCore3857
    )

    # WHY 先复制再喂 CLI：N115 实测 vector info 会在**被喂进去的那份输入旁边**写 .aux.xml
    # （GPKG 会写、GeoJSON 不写，写不写取决于驱动，不是"矢量都不写"）。
    # data/ 只许被复制 ⇒ 本批所有输入落进 build/tmp/c3a/，边车也种在那里。
    $scratchRoot = Join-Path $ResolvedWorkspaceRoot "build\tmp\c3a"
    New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

    $vectorDataRoot = Join-Path $ResolvedWorkspaceRoot "data\vector"
    Require-Path -Path $vectorDataRoot -Label "data/vector"

    # 面输入：工单指定的 data/vector/中国_省.geojson。名字按码点拼而不写字面中文 ——
    # 这份 .ps1 是无 BOM 的 UTF-8，PowerShell 5.1 按 ANSI 代码页解码源文件，字面中文路径
    # 在这台机器上会变成乱码（与本仓"PS5.1 不带 -Encoding 读坏 UTF-8"是同一条成因）。
    $provinceName = (-join @([char]0x4E2D, [char]0x56FD, [char]0x005F, [char]0x7701)) + ".geojson"
    $provinceRaw = Join-Path $vectorDataRoot $provinceName
    if (-not (Test-Path $provinceRaw)) {
        # 指定文件不在了也要红着说明，不许静默换一个图层继续跑。
        $fallback = Get-ChildItem $vectorDataRoot -Filter "*.geojson" |
            Sort-Object Length |
            Select-Object -First 1
        Assert-Condition -Condition ($null -ne $fallback) -Message (
            "derived geometry leg: province input missing and no *.geojson fallback in " + $vectorDataRoot)
        $provinceRaw = $fallback.FullName
    }

    $polygonInput = Join-Path $scratchRoot "province.geojson"
    Copy-Item $provinceRaw $polygonInput -Force
    $lineInput = Join-Path $scratchRoot "roads_3857.gpkg"
    Copy-Item $RoadsCore3857 $lineInput -Force

    # 三个 extract 跑在"真实线的切片"上：整层 22836 要素在 Debug 树实测
    # vertices 563.9 s + endpoints 115.9 s + midpoints 105.5 s = 785.3 s，
    # 单这三条就顶穿本腿 720 s 的 TIMEOUT（本腿今天整条 Debug 热跑 170.935 s）⇒ 取真实图层的
    # 前 800 个要素做切片（实测切片本身 0.9 s、三条合计 15.6 s）。切片仍由真实几何构成，
    # 判据也全部从切片自己的现读属性推导，不写死数字。
    $lineSlice = Join-Path $scratchRoot "roads_3857_slice.gpkg"
    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_derived_line_slice" -CaseArgs @(
        "vector", "filter",
        ("--input=" + $lineInput),
        ("--output=" + $lineSlice),
        "--where=fid <= 800"
    )

    $outputs = [ordered]@{
        hull_polygon     = Join-Path $scratchRoot "out_hull_polygon.gpkg"
        envelope_polygon = Join-Path $scratchRoot "out_envelope_polygon.gpkg"
        centroid_polygon = Join-Path $scratchRoot "out_centroid_polygon.gpkg"
        interior_polygon = Join-Path $scratchRoot "out_interior_polygon.gpkg"
        boundary_polygon = Join-Path $scratchRoot "out_boundary_polygon.gpkg"
        vertices_line    = Join-Path $scratchRoot "out_vertices_line.gpkg"
        midpoints_line   = Join-Path $scratchRoot "out_midpoints_line.gpkg"
        endpoints_line   = Join-Path $scratchRoot "out_endpoints_line.gpkg"
    }

    # 输入侧现读：面用 data/vector 的省份图层，线用真实道路图层的切片。
    # 判据的全部期望值都从这两个对象推出来 —— 换一个数据文件它们照样成立，写死数字就做不到。
    $polygonStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $polygonInput -ScratchDir $scratchRoot
    $lineStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $lineSlice -ScratchDir $scratchRoot
    Assert-Condition -Condition ($lineStats.Geometry -match "LineString") -Message (
        "derived geometry leg: line slice geometry is " + $lineStats.Geometry +
        ", not a line layer (extract 三兄弟只对线图层有定义，喂错图层必须红)")
    Assert-Condition -Condition ($polygonStats.FeatureCount -gt 0) -Message "derived geometry leg: polygon input has no feature"
    Assert-Condition -Condition ($lineStats.FeatureCount -gt 0) -Message "derived geometry leg: line slice has no feature"

    return [pscustomobject]@{
        ScratchRoot = $scratchRoot
        PolygonInput = $polygonInput
        LineInput = $lineInput
        LineSlice = $lineSlice
        Outputs = $outputs
        PolygonStats = $polygonStats
        LineStats = $lineStats
    }
}

function Validate-CaseOutputs {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [string]$ResolvedOutputRoot,
        [int]$InputFeatureCount,
        [object]$DerivedRefs
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
        # ── C3a 几何派生 8 条：判据全部由输入图层自己的现读属性推导 ──
        "hull_polygon" {
            Assert-DerivedGeometryCase -Name "hull_polygon" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["hull_polygon"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.PolygonStats `
                -ExpectedGeometryPattern "Polygon" -ExtentMode "equal" -FeaturesMode "equal" | Out-Null
        }
        "envelope_polygon" {
            Assert-DerivedGeometryCase -Name "envelope_polygon" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["envelope_polygon"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.PolygonStats `
                -ExpectedGeometryPattern "Polygon" -ExtentMode "equal" -FeaturesMode "equal" | Out-Null
        }
        "centroid_polygon" {
            Assert-DerivedGeometryCase -Name "centroid_polygon" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["centroid_polygon"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.PolygonStats `
                -ExpectedGeometryPattern "Point" -ExtentMode "inside" -FeaturesMode "equal" | Out-Null
        }
        "interior_polygon" {
            Assert-DerivedGeometryCase -Name "interior_polygon" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["interior_polygon"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.PolygonStats `
                -ExpectedGeometryPattern "Point" -ExtentMode "inside" -FeaturesMode "equal" | Out-Null
        }
        "boundary_polygon" {
            Assert-DerivedGeometryCase -Name "boundary_polygon" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["boundary_polygon"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.PolygonStats `
                -ExpectedGeometryPattern "^(Multi)?LineString$" -ExtentMode "inside" -FeaturesMode "equal" | Out-Null
        }
        "vertices_line" {
            $read = Assert-DerivedGeometryCase -Name "vertices_line" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["vertices_line"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.LineStats `
                -ExpectedGeometryPattern "Point" -ExtentMode "inside" -FeaturesMode "none"
            $script:DerivedVertexCount = $read.FeatureCount
        }
        "midpoints_line" {
            $read = Assert-DerivedGeometryCase -Name "midpoints_line" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["midpoints_line"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.LineStats `
                -ExpectedGeometryPattern "Point" -ExtentMode "inside" -FeaturesMode "none"
            $script:DerivedMidpointCount = $read.FeatureCount
            Validate-DerivedGeometryRelations -Name "midpoints_line" `
                -VertexCount $script:DerivedVertexCount -EndpointCount $script:DerivedEndpointCount `
                -MidpointCount $script:DerivedMidpointCount -InputFeatureCount $DerivedRefs.LineStats.FeatureCount
        }
        "endpoints_line" {
            $read = Assert-DerivedGeometryCase -Name "endpoints_line" -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$DerivedRefs.Outputs["endpoints_line"]) -ScratchDir $DerivedRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $DerivedRefs.LineStats `
                -ExpectedGeometryPattern "Point" -ExtentMode "inside" -FeaturesMode "none"
            $script:DerivedEndpointCount = $read.FeatureCount
            Validate-DerivedGeometryRelations -Name "endpoints_line" `
                -VertexCount $script:DerivedVertexCount -EndpointCount $script:DerivedEndpointCount `
                -MidpointCount $script:DerivedMidpointCount -InputFeatureCount $DerivedRefs.LineStats.FeatureCount
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

# ── C3a：矢量几何派生 8 动作的真实数据链路（每动作 1 条 quick 用例）──
# 输入全部是复制件（build/tmp/c3a/），data/ 只读；判据见 Assert-DerivedGeometryCase 的注释块。
# 切片、现读属性、输出路径都由 Ensure-DerivedGeometryInputs 一次性带回来（属性在返回对象上
# 一次建齐；PS 5.1 的 pscustomobject 事后加属性会报"找不到可设置的属性"，这是本机事实）。
$derivedRefs = Ensure-DerivedGeometryInputs -ResolvedCliPath $ResolvedCliPath `
    -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -RoadsCore3857 $roadsCore3857
$script:DerivedVertexCount = -1
$script:DerivedMidpointCount = -1
$script:DerivedEndpointCount = -1

function New-DerivedCase {
    param(
        [string]$Name,
        [string]$Action,
        [string]$InputPath,
        [string]$OutputPath
    )

    return New-Case -Name $Name -CaseArgs @(
        "vector", $Action,
        ("--input=" + $InputPath),
        ("--output=" + $OutputPath)
    ) -ExpectedOutputs @($OutputPath)
}

$cases += New-DerivedCase -Name "hull_polygon" -Action "convex_hull" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["hull_polygon"]
$cases += New-DerivedCase -Name "envelope_polygon" -Action "envelope" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["envelope_polygon"]
$cases += New-DerivedCase -Name "centroid_polygon" -Action "centroid" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["centroid_polygon"]
$cases += New-DerivedCase -Name "interior_polygon" -Action "interior_point" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["interior_polygon"]
$cases += New-DerivedCase -Name "boundary_polygon" -Action "boundary" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["boundary_polygon"]
# 顺序即依赖：vertices 先跑，midpoints/endpoints 的关系判据要用它的现读数。
$cases += New-DerivedCase -Name "vertices_line" -Action "vertices_extract" `
    -InputPath $derivedRefs.LineSlice -OutputPath $derivedRefs.Outputs["vertices_line"]
$cases += New-DerivedCase -Name "midpoints_line" -Action "midpoints_extract" `
    -InputPath $derivedRefs.LineSlice -OutputPath $derivedRefs.Outputs["midpoints_line"]
$cases += New-DerivedCase -Name "endpoints_line" -Action "endpoints_extract" `
    -InputPath $derivedRefs.LineSlice -OutputPath $derivedRefs.Outputs["endpoints_line"]

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
    Validate-CaseOutputs -Case $result -ResolvedCliPath $ResolvedCliPath -ResolvedOutputRoot $ResolvedOutputRoot -InputFeatureCount $InputFeatureCount -DerivedRefs $derivedRefs
    $results += $result
}

Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Vector regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(20) + " " + $result.Seconds + "s")
}
Write-Host ("  summary_json".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".json")))
Write-Host ("  summary_csv".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".csv")))
