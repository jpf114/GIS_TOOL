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
    # 字段数顺手带回来（可选：抓不到留 null，谁要用谁自己判红）——C3b-A 的 geom_metrics 臂要拿
    # "输出字段数 == 输入字段数 + 5" 核它的产品契约，而其他腿不该被这条新解析拖住。
    $fieldCount = [regex]::Match($text, "(?m)^\s*Fields:\s+(\d+)\s*$")
    $fields = if ($fieldCount.Success) { [int]$fieldCount.Groups[1].Value } else { $null }

    return [pscustomobject]@{
        FeatureCount = [int]$count.Groups[1].Value
        FieldCount = $fields
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

    # 面输入：工单指定的 data/vector/中国_省.geojson（查找与兜底逻辑抽给 C3b-A 共用，见
    # Resolve-ProvinceVectorInput；两条判据消息的文案与抽出前逐字一致）。
    $provinceRaw = Resolve-ProvinceVectorInput -VectorDataRoot $vectorDataRoot -Label "derived geometry leg"

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

function Resolve-ProvinceVectorInput {
    param(
        [string]$VectorDataRoot,
        [string]$Label
    )

    Require-Path -Path $VectorDataRoot -Label "data/vector"

    # 名字按码点拼而不写字面中文 —— 这份 .ps1 是无 BOM 的 UTF-8，PowerShell 5.1 按 ANSI 代码页解码源文件，
    # 字面中文路径在这台机器上会变成乱码（与本仓"PS5.1 不带 -Encoding 读坏 UTF-8"是同一条成因）。
    $provinceName = (-join @([char]0x4E2D, [char]0x56FD, [char]0x005F, [char]0x7701)) + ".geojson"
    $provinceRaw = Join-Path $VectorDataRoot $provinceName
    if (-not (Test-Path $provinceRaw)) {
        # 指定文件不在了也要红着说明，不许静默换一个图层继续跑。
        $fallback = Get-ChildItem $VectorDataRoot -Filter "*.geojson" |
            Sort-Object Length |
            Select-Object -First 1
        Assert-Condition -Condition ($null -ne $fallback) -Message (
            $Label + ": province input missing and no *.geojson fallback in " + $VectorDataRoot)
        $provinceRaw = $fallback.FullName
    }

    return $provinceRaw
}

# ── C3b-A：vector 拓扑/检查批 11 动作的真实数据腿（只在 Mode=full 注册，即 real_data_vector_full）──
# 派生件（全部由本脚本现造，一个都不假设上一轮探针留下过什么）：
#   w212            = roads 3857 整层（22,836 条线）上取 20×20 km 窗 ⇒ 212 条线；
#   buf30           = w212 的 30 m 缓冲 ⇒ 212 个 MultiPolygon（"面与面真重叠"的样本）；
#   prov3857        = data/vector/中国_省.geojson 重投影到 EPSG:3857 ⇒ 42 要素、图层声明 Unknown（34 面 + 8 线）；
#   province_boundary = prov3857 的 boundary ⇒ 42 条闭合环线（每个环首末点同坐标）；
#   ring_endpoints   = boundary 的 endpoints_extract ⇒ 1134 个点（= 2 × 567 个闭合环）。
# WHY 还要多造 boundary 与 ring_endpoints 两个派生件：真实数据里**没有**"必然没有悬空端点"的线层，
#   也没有"必然存在重复点"的点层，而工单要求判据是关系式、不许写"我猜 0"。闭合环这两条是拓扑定理级的：
#     ① 悬空判定看"端点坐标出现次数 == 1"，闭合环的 StartPoint 与 EndPoint 逐字节相同 ⇒ 悬空必然为 0；
#     ② 重复判定按 pointKey 分组、每组写 (n-1) 行；567 个环各贡献一对重合点 ⇒ 去重键数 <= 1134/2，
#        于是 issue = Σ(n_k-1) = 总点数 - 去重键数 >= 总点数/2。
#   ⇒ "把所有端点都当悬空""issue_count 写成常量 0""根本不写行""只在有长度时才写行"这几个方向
#     各被钉死一次，而且三条判据都与样本里恰好有什么无关。
# WHY 面类动作喂 Geometry: Unknown 的混合层：工单 2026-10-22 现读确认四个面动作的类型闸都走
#   effectiveLayerGeometryType（从要素推断），Unknown 层照样 Success ⇒ 混层不是"绕过闸"，是多一条真实覆盖。
# WHY 判据成对写（工单 C3b-A）：overlap(buf30) >= 1 / overlap(prov3857) 修复前实测 == 0、C3d 修复后 >= 1 /
#   adjacency(prov3857) >= 1 / adjacency(buf30) == 0 四条一起，才既不是"猜 0"也不是给这份样本标定的常量。
# ⚠ 产品口径差 N125（工单 C3b-A 现读到并登记、**C3d 已修复销账**）：overlap_check 曾自带一份只认
#   Polygon/MultiPolygon 的交面积 switch，交集是 GeometryCollection 时落 default 得 0 ⇒ 不写行；而
#   topology_check 走 polygonIntersectionArea()（common.inc:499-509）显式累加 GeometryCollection 的子面面积。
#   prov3857 上修复前实测 overlap 报 0、topology 报 1 条（12,14,overlap,401282）⇒ 同一对要素两个动作自相矛盾。
#   C3d 让 overlap_check 复用同一个 polygonIntersectionArea()（**单源**，本地 switch 已删除）⇒ 省界臂现读
#   写出 12,14,401282，与 topology 那一行同值 ⇒ 跨动作等式不再需要"只在 buf30 那一臂写"的例外；
#   但 9d98497 留下的 ">=" 单向关系**原样保留**（修好后依然成立、且保持可追溯），等式由 C3d 新增断言承重。

function Get-CsvTable {
    param([string]$Path)

    # 不用 Import-Csv：它按表头建属性名，列一旦改名或换序就"读得到但读错列"。
    # 本批要钉的恰恰是"第几列是什么"（C3b-B1/B2 在单测侧钉的是拆列），真实腿必须同形 ⇒
    # 只按位置拆列，表头整串核对，数字一律 TryParse，解析失败即红（不许归零、不许 Contains 子串）。
    $rawLines = @(Get-Content -Path $Path -Encoding UTF8)
    Assert-Condition -Condition ($rawLines.Count -ge 1) -Message (" CSV has no header line:" + $Path)

    $rows = @()
    for ($i = 1; $i -lt $rawLines.Count; $i++) {
        $rows += ,(@($rawLines[$i] -split ","))
    }

    return [pscustomobject]@{
        Path = $Path
        Header = @($rawLines[0] -split ",")
        Rows = $rows
        DataRowCount = $rows.Count
    }
}

function Assert-CsvHeader {
    param(
        [object]$Table,
        [string]$Expected,
        [string]$Label
    )

    $actual = ($Table.Header -join ",")
    Assert-Condition -Condition ($actual -ceq $Expected) -Message (
        $Label + ": CSV header is '" + $actual + "', expected exactly '" + $Expected +
        "' ( )")
}

function Get-CsvColumn {
    param(
        [object]$Table,
        [string]$Name,
        [string]$Label
    )

    $index = [Array]::IndexOf($Table.Header, $Name)
    Assert-Condition -Condition ($index -ge 0) -Message (
        $Label + ": CSV column '" + $Name + "' missing; header is " + ($Table.Header -join ","))

    $values = @()
    foreach ($row in $Table.Rows) {
        Assert-Condition -Condition ($row.Count -eq $Table.Header.Count) -Message (
            $Label + ": CSV row '" + ($row -join ",") + "' does not have " + $Table.Header.Count + " columns")
        $values += [string]$row[$index]
    }

    return $values
}

function Get-CsvDouble {
    param(
        [string]$Value,
        [string]$Label
    )

    # 不变文化解析：CSV 由 C++ 的 ostream 写，小数点恒为 "."，而且 3857 的度量坐标被打成
    # "1.29663e+07" 这种指数形态（NumberStyles Float 含 AllowExponent）。
    # 用当前区域设置解析会在逗号作小数点的机器上把 "2826.14" 解成 282614 ⇒ 判据不许依赖宿主区域设置。
    $parsed = [double]0
    $ok = [double]::TryParse($Value, [System.Globalization.NumberStyles]::Float,
        [System.Globalization.CultureInfo]::InvariantCulture, [ref]$parsed)
    Assert-Condition -Condition ($ok -and (-not ([double]::IsNaN($parsed) -or [double]::IsPositiveInfinity($parsed) -or [double]::IsNegativeInfinity($parsed)))) -Message (
        $Label + ": CSV value '" + $Value + "' is not a finite number")
    return $parsed
}

function Get-CsvInt64 {
    param(
        [string]$Value,
        [string]$Label
    )

    $parsed = [long]0
    $ok = [long]::TryParse($Value, [System.Globalization.NumberStyles]::Integer,
        [System.Globalization.CultureInfo]::InvariantCulture, [ref]$parsed)
    Assert-Condition -Condition $ok -Message ($Label + ": CSV value '" + $Value + "' is not an integer")
    return $parsed
}

function Assert-CsvRowsEqualReported {
    param(
        [object]$Table,
        [int]$Reported,
        [string]$Label
    )

    # 工单 C3b-A："relation_count / issue_count 一律与 CSV 行数对撞（行数 == 键值 + 1）"，多的一行是表头。
    # 两边都是现读：键值走 CLI stdout 的整行锚定正则（Get-ReportedMetadataCount），行数走产物文件本身。
    Assert-Condition -Condition ($Table.DataRowCount -eq $Reported) -Message (
        $Label + ": CSV has " + $Table.DataRowCount + " data row(s) (total " + ($Table.DataRowCount + 1) +
        "line(s)) but the action reported " + $Reported)
}

function Assert-CsvFidPairsAscending {
    param(
        [object]$Table,
        [string]$Label
    )

    # 关系类 CSV 前两列必然 source_fid < target_fid：三个 O(n²) 动作都走 i<j 的双层下标循环，
    # 而 GPKG 的 FID 按写入顺序创建 ⇒ 循环序就是 FID 升序。把两列写成同值（C3b-2 的 M-7a 那一族）
    # 或写反顺序都逃不过这条；它与"多少行"无关，基数判据与列位判据互相补位。
    $bad = @()
    for ($i = 0; $i -lt $Table.DataRowCount; $i++) {
        $row = @($Table.Rows[$i])
        $source = Get-CsvInt64 -Value ([string]$row[0]) -Label ($Label + " row " + ($i + 2) + " source_fid")
        $target = Get-CsvInt64 -Value ([string]$row[1]) -Label ($Label + " row " + ($i + 2) + " target_fid")
        if (-not ($source -lt $target)) {
            $bad += ($source.ToString() + "!<" + $target.ToString())
        }
    }
    Assert-Condition -Condition ($bad.Count -eq 0) -Message (
        $Label + ": FID " + $bad.Count + " " + ($bad -join ";"))
}

function Assert-CsvPointColumnsInsideExtent {
    param(
        [object]$Table,
        [string]$XColumn,
        [string]$YColumn,
        [object]$Stats,
        [double]$Tolerance,
        [string]$Label
    )

    # 坐标列必须落在**输入图层自己的**范围内（vector info 现读的 Extent）。
    # 这一条专治"把 x/y 写成常量 0"（C3b-2 的 M-4a 那一族）：3857 的度量坐标是 1e7 量级，
    # 写死 0 立刻被"在范围外"抓住；而"拆列后逐行核"覆盖全部行，比"整串比某一行"强。
    # 容差取范围跨度的 1%（这里跨度约 3.5e4 m ⇒ 350 m）：足以吸收 info/CSV 只打 6 位有效数字造成的
    # 截断（同量级最大约 10 m），又与 0 相差六个数量级 ⇒ 不会把判据放宽成恒真。
    $spanX = [Math]::Max([Math]::Abs($Stats.MaxX - $Stats.MinX), 1.0)
    $spanY = [Math]::Max([Math]::Abs($Stats.MaxY - $Stats.MinY), 1.0)
    $xs = @(Get-CsvColumn -Table $Table -Name $XColumn -Label $Label)
    $ys = @(Get-CsvColumn -Table $Table -Name $YColumn -Label $Label)
    $outliers = @()
    for ($i = 0; $i -lt $xs.Count; $i++) {
        $x = Get-CsvDouble -Value $xs[$i] -Label ($Label + " row " + ($i + 2) + " " + $XColumn)
        $y = Get-CsvDouble -Value $ys[$i] -Label ($Label + " row " + ($i + 2) + " " + $YColumn)
        if ($x -lt ($Stats.MinX - $Tolerance * $spanX) -or $x -gt ($Stats.MaxX + $Tolerance * $spanX) -or
            $y -lt ($Stats.MinY - $Tolerance * $spanY) -or $y -gt ($Stats.MaxY + $Tolerance * $spanY)) {
            $outliers += (" " + ($i + 2) + "(" + $x + "," + $y + ")")
        }
    }
    Assert-Condition -Condition ($outliers.Count -eq 0) -Message (
        $Label + ":" + $outliers.Count + " " + (Format-Extent $Stats) +
        " " + $Tolerance + " ")
}

function Get-ReportedMetadataValue {
    param(
        [string]$Text,
        [string]$Key,
        [string]$Label
    )

    # 与 Get-ReportedMetadataCount 同一条整行锚定，但取字符串值（nearest_vector 这类"入参回显"键）。
    $match = [regex]::Match($Text, ("(?m)^\s*" + $Key + ":\s*(.+?)\s*$"))
    Assert-Condition -Condition ($match.Success) -Message (
        $Label + ": metadata key '" + $Key + "' absent; raw output was:" + $Text)
    return $match.Groups[1].Value
}

function Get-LayerFeatureCount {
    param(
        [string]$ResolvedCliPath,
        [string]$Path,
        [string]$ScratchDir
    )

    # sliver_remove 把 212 个部件全删掉那一臂产出**空图层**，而 vector info 对空图层不打印 Extent 行 ⇒
    # 走 Get-LayerStats 必判红（红在读取器上而不是红在判据上）。这里只取 Features 行，同样整行锚定、抓不到即红。
    $text = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
        "vector", "info",
        ("--input=" + $Path),
        ("--output=" + (Join-Path $ScratchDir "feature_count_placeholder.gpkg"))
    )
    $count = [regex]::Match($text, "(?m)^\s*Features:\s+(\d+)\s*$")
    Assert-Condition -Condition ($count.Success) -Message (" no Features line in info output for " + $Path)
    return [int]$count.Groups[1].Value
}

function Assert-TopologyIssueCsvShape {
    param(
        [object]$Table,
        [ref]$PairRowCount,
        [string]$Label
    )

    # topology_check 的 CSV 混装四类行，实现里的口径是确定的：
    #   empty_geometry / invalid_geometry 各占一行、target 列为空、issue_value 写死 0（两个独立 if，可同写两行）；
    #   duplicate_geometry（Equals 分支）与 overlap（else-if 分支）互斥、都是成对行且 FID 升序，
    #   duplicate 写 0、overlap 写交面积（> 0 才写行）。
    # ⇒ 逐行核"类型在四类里 + 成对与否 + 数值是否为 0"同时钉住这四个分支的写法（挂账 N123 说的
    #   empty/invalid 双行语义在这里只核"写法"，不猜"某个几何会不会同时被判两种"）。
    $types = @(Get-CsvColumn -Table $Table -Name "issue_type" -Label $Label)
    $targets = @(Get-CsvColumn -Table $Table -Name "target_fid" -Label $Label)
    $values = @(Get-CsvColumn -Table $Table -Name "issue_value" -Label $Label)
    $allowed = @("empty_geometry", "invalid_geometry", "duplicate_geometry", "overlap")
    $pairs = 0
    for ($i = 0; $i -lt $Table.DataRowCount; $i++) {
        $type = $types[$i]
        Assert-Condition -Condition ($allowed -ccontains $type) -Message (
            $Label + ":" + ($i + 2) + " issue_type '" + $type + "'")
        $value = Get-CsvDouble -Value $values[$i] -Label ($Label + " row " + ($i + 2) + " issue_value")
        if ($type -eq "overlap") {
            Assert-Condition -Condition ($value -gt 0.0) -Message (
                $Label + ":" + ($i + 2) + " overlap issue_value " + $value + "> 0 <= 0")
            Assert-Condition -Condition ([string]$targets[$i] -ne " ") -Message (
                $Label + ":" + ($i + 2) + " overlap target_fid")
            $pairs++
        } elseif ($type -eq "duplicate_geometry") {
            Assert-Condition -Condition ($value -eq 0.0) -Message (
                $Label + ":" + ($i + 2) + " duplicate_geometry issue_value " + $value + " 0")
            Assert-Condition -Condition ([string]$targets[$i] -ne " ") -Message (
                $Label + ":" + ($i + 2) + " duplicate_geometry target_fid")
            $pairs++
        } else {
            Assert-Condition -Condition ($value -eq 0.0) -Message (
                $Label + ":" + ($i + 2) + " " + $type + " issue_value " + $value + " 0")
            Assert-Condition -Condition ([string]$targets[$i] -eq " ") -Message (
                $Label + ":" + ($i + 2) + " " + $type + " target '" + $targets[$i] + "'")
        }
    }
    $PairRowCount.Value = $pairs
}

function New-CheckCase {
    param(
        [string]$Name,
        [string]$Action,
        [string]$InputPath,
        [string]$OutputPath,
        [string[]]$ExtraArgs
    )

    # 不往名为 $args 的变量里写 —— 它是 PowerShell 的自动变量，遮蔽后排查成本极高。
    $caseArgs = @(
        "vector", $Action,
        ("--input=" + $InputPath),
        ("--output=" + $OutputPath)
    )
    if ($null -ne $ExtraArgs) {
        foreach ($extra in $ExtraArgs) {
            $caseArgs += $extra
        }
    }

    return New-Case -Name $Name -CaseArgs $caseArgs -ExpectedOutputs @($OutputPath)
}

function Ensure-TopologyCheckInputs {
    param(
        [string]$ResolvedCliPath,
        [string]$ResolvedWorkspaceRoot,
        [string]$RoadsCore3857
    )

    # 派生件一律现造、不缓存：控制器 2026-10-22 的探针在 build/tmp/c3ba 留过同名文件，
    # "上次造过所以这次跳过"会让这条腿在清过 build 的机器上行为不同，也会让耗时低估
    # —— 而工单要求加完腿之后**现跑现报**该条的墙钟。
    $scratchRoot = Join-Path $ResolvedWorkspaceRoot "tmp\vector_benchmark\c3ba"
    if (Test-Path $scratchRoot) {
        Remove-Item -Recurse -Force $scratchRoot
    }
    New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

    # data/ 只读（N115）：喂 CLI 的一律是复制件，边车（.aux.xml）只可能种在本腿的临时区。
    $vectorDataRoot = Join-Path $ResolvedWorkspaceRoot "data\vector"
    $provinceRaw = Resolve-ProvinceVectorInput -VectorDataRoot $vectorDataRoot -Label "topology check leg"
    $provinceGeojson = Join-Path $scratchRoot "province_raw.geojson"
    Copy-Item $provinceRaw $provinceGeojson -Force

    $lineWindow = Join-Path $scratchRoot "w212.gpkg"
    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_line_window" -CaseArgs @(
        "vector", "filter",
        ("--input=" + $RoadsCore3857),
        ("--output=" + $lineWindow),
        "--extent=12950000,4890000,12970000,4910000"
    )

    $bufferPolygons = Join-Path $scratchRoot "buf30.gpkg"
    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_buffer_polygons" -CaseArgs @(
        "vector", "buffer",
        ("--input=" + $lineWindow),
        ("--output=" + $bufferPolygons),
        "--distance=30"
    )

    $provinceProjected = Join-Path $scratchRoot "prov3857.gpkg"
    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_projected_provinces" -CaseArgs @(
        "projection", "reproject",
        ("--input=" + $provinceGeojson),
        ("--output=" + $provinceProjected),
        "--dst_srs=EPSG:3857"
    )

    $provinceBoundary = Join-Path $scratchRoot "province_boundary.gpkg"
    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_province_boundary" -CaseArgs @(
        "vector", "boundary",
        ("--input=" + $provinceProjected),
        ("--output=" + $provinceBoundary)
    )

    $ringEndpoints = Join-Path $scratchRoot "ring_endpoints.gpkg"
    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_ring_endpoints" -CaseArgs @(
        "vector", "endpoints_extract",
        ("--input=" + $provinceBoundary),
        ("--output=" + $ringEndpoints)
    )

    # 判据的全部期望值都从这五个现读对象推出来 ⇒ 换一份数据文件不需要改判据，写死数字就做不到。
    $windowStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $lineWindow -ScratchDir $scratchRoot
    $bufferStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $bufferPolygons -ScratchDir $scratchRoot
    $provinceStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $provinceProjected -ScratchDir $scratchRoot
    $boundaryStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $provinceBoundary -ScratchDir $scratchRoot
    $endpointStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $ringEndpoints -ScratchDir $scratchRoot

    # 喂错图层会把"红在算法上"变成"红在闸上"，判据就失去信息量 ⇒ 先按现读类型核五件事。
    Assert-Condition -Condition ($windowStats.Geometry -match "LineString") -Message (
        "topology check leg: window layer is " + $windowStats.Geometry + ", not a line layer " +
        "dangling_endpoint_check nearest")
    Assert-Condition -Condition ($bufferStats.Geometry -match "Polygon") -Message (
        "topology check leg: buffer layer is " + $bufferStats.Geometry + ", not a polygon layer")
    # 关系数在单要素图层上恒 0（工单点名 china_bbox_3857 只有 1 个面 ⇒ 判据没有信息量）。
    Assert-Condition -Condition ($bufferStats.FeatureCount -gt 1 -and $provinceStats.FeatureCount -gt 1) -Message (
        "topology check leg: O(n ) 2 buf30 " + $bufferStats.FeatureCount +
        "/ prov3857 " + $provinceStats.FeatureCount)
    # 混合图层是本刀的**有意**覆盖（工单前提 ①：面类动作的几何类型闸走要素推断，Unknown 层照样 Success）。
    Assert-Condition -Condition ($provinceStats.Geometry -match "Unknown") -Message (
        "topology check leg: province layer declares " + $provinceStats.Geometry +
        "instead of the mixed (Geometry: Unknown) layer this batch deliberately feeds through the polygon gates")
    Assert-Condition -Condition ($boundaryStats.Geometry -match "LineString") -Message (
        "topology check leg: boundary layer is " + $boundaryStats.Geometry + ", expected a line layer")
    Assert-Condition -Condition ($endpointStats.Geometry -match "^Point$") -Message (
        "topology check leg: endpoints layer is " + $endpointStats.Geometry + ", expected Point " +
        "duplicate_point_check skipped_geometry_count == 0 " +
        "' OGRPoint'")
    Assert-Condition -Condition ($endpointStats.FeatureCount -ge 2 -and ($endpointStats.FeatureCount % 2) -eq 0) -Message (
        "topology check leg: endpoints Features " + $endpointStats.FeatureCount +
        "2")

    $outputs = [ordered]@{
        multipart_prov_csv  = Join-Path $scratchRoot "c3ba_multipart_prov.csv"
        multipart_buf_csv   = Join-Path $scratchRoot "c3ba_multipart_buf.csv"
        hole_prov_csv       = Join-Path $scratchRoot "c3ba_hole_prov.csv"
        hole_buf_csv        = Join-Path $scratchRoot "c3ba_hole_buf.csv"
        dangling_window_csv = Join-Path $scratchRoot "c3ba_dangling_window.csv"
        dangling_ring_csv   = Join-Path $scratchRoot "c3ba_dangling_ring.csv"
        overlap_buf_csv     = Join-Path $scratchRoot "c3ba_overlap_buf.csv"
        overlap_prov_csv    = Join-Path $scratchRoot "c3ba_overlap_prov.csv"
        topology_buf_csv    = Join-Path $scratchRoot "c3ba_topology_buf.csv"
        topology_prov_csv   = Join-Path $scratchRoot "c3ba_topology_prov.csv"
        duplicate_ring_csv  = Join-Path $scratchRoot "c3ba_duplicate_ring.csv"
        adjacency_prov_csv  = Join-Path $scratchRoot "c3ba_adjacency_prov.csv"
        adjacency_buf_csv   = Join-Path $scratchRoot "c3ba_adjacency_buf.csv"
        singlepart_prov     = Join-Path $scratchRoot "c3ba_singlepart_prov.gpkg"
        sliver_low          = Join-Path $scratchRoot "c3ba_sliver_low.gpkg"
        sliver_high         = Join-Path $scratchRoot "c3ba_sliver_high.gpkg"
        metrics_buf         = Join-Path $scratchRoot "c3ba_geom_metrics.gpkg"
        nearest_self        = Join-Path $scratchRoot "c3ba_nearest.gpkg"
    }

    return [pscustomobject]@{
        ScratchRoot = $scratchRoot
        LineWindow = $lineWindow
        BufferPolygons = $bufferPolygons
        ProvinceProjected = $provinceProjected
        ProvinceBoundary = $provinceBoundary
        RingEndpoints = $ringEndpoints
        WindowStats = $windowStats
        BufferStats = $bufferStats
        ProvinceStats = $provinceStats
        BoundaryStats = $boundaryStats
        EndpointStats = $endpointStats
        Outputs = $outputs
    }
}

# C3b-A 的 18 条用例名单：注册与判据共用同一份名单 ⇒ "进了一条没有判据的用例"会当场红
# （正向由条数对撞、反向由 Assert-TopologyCheckCase 的 default 分支），这正是本批一直在消灭的
# "没检查长得像检查通过"。11 个动作 ⇒ 有对照关系的动作各占两条臂（overlap/topology/hole/adjacency/
# multipart/dangling）与两条阈值臂（sliver_remove），合 18 条用例。
$script:TopologyCheckCaseNames = @(
    "multipart_check_prov",
    "singlepart_prov",
    "multipart_check_buf",
    "hole_check_prov",
    "hole_check_buf",
    "overlap_check_buf",
    "topology_check_buf",
    "overlap_check_prov",
    "topology_check_prov",
    "adjacency_prov",
    "adjacency_buf",
    "dangling_endpoint_w212",
    "dangling_endpoint_closed_ring",
    "duplicate_point_ring_endpoints",
    "geom_metrics_buf",
    "sliver_remove_buf_low_area",
    "sliver_remove_buf_high_area",
    "nearest_w212_self"
)

function Assert-TopologyCheckCase {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [object]$TopologyRefs
    )

    if ($script:TopologyCheckCaseNames -notcontains $Case.Name) {
        return
    }

    Assert-Condition -Condition ($null -ne $TopologyRefs) -Message (
        $Case.Name + ": C3b-A / TopologyRefs null " +
        "Mode=full Mode")

    switch ($Case.Name) {
        "multipart_check_prov" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["multipart_prov_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,geom_type,part_count" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            $scanned = Get-ReportedMetadataCount -Text $Case.Output -Key "scanned_count" -Label ($Case.Name + " scanned_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            # 工单 C3b-A 指定的关系式：issue_count == scanned_count == Features。
            # 42/42 全命中是这份数据的真实形状（判定是 partCount>1 || isMultipartGeometryType ⇒
            # 单部件 MultiPolygon 也算多部件），**不是**"只有跨海/飞地省才是多部件"那种我以为的常识。
            # 三条必须一起核：只写 issue == scanned 的话"什么都不报"（0 == 0）也过。
            $features = [int]$TopologyRefs.ProvinceStats.FeatureCount
            Assert-Condition -Condition ($issue -eq $scanned -and $issue -eq $features) -Message (
                $Case.Name + ": issue_count " + $issue + "/ scanned_count " + $scanned +
                "/ Features " + $features + " ")
            # 列级：每个被列出的要素至少 1 个部件（两种判定分支都蕴含 >= 1）。
            $parts = @(Get-CsvColumn -Table $table -Name "part_count" -Label $Case.Name)
            $partSum = 0
            foreach ($value in $parts) {
                $count = Get-CsvInt64 -Value $value -Label ($Case.Name + " part_count")
                Assert-Condition -Condition ($count -ge 1) -Message (
                    $Case.Name + ": part_count " + $count + "< 1")
                $partSum += $count
            }
            Assert-Condition -Condition ($partSum -ge $issue) -Message (
                $Case.Name + ": part_count " + $partSum + "<" + $issue)
            # 部件总数留给 singlepart 做跨动作对撞（见 singlepart_prov 臂）。
            $script:C3baMultipartProvParts = $partSum
        }
        "singlepart_prov" {
            Assert-Condition -Condition ($script:C3baMultipartProvParts -ge 0) -Message (
                $Case.Name + " ran before multipart_check_prov, cannot cross-check part counts")
            $read = Assert-DerivedGeometryCase -Name $Case.Name -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$TopologyRefs.Outputs["singlepart_prov"]) -ScratchDir $TopologyRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $TopologyRefs.ProvinceStats `
                -ExpectedGeometryPattern "^Polygon$" -ExtentMode "equal" -FeaturesMode "none"
            $feature = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label ($Case.Name + " feature_count")
            $source = Get-ReportedMetadataCount -Text $Case.Output -Key "source_count" -Label ($Case.Name + " source_count")
            $skipped = Get-ReportedMetadataCount -Text $Case.Output -Key "skipped_count" -Label ($Case.Name + " skipped_count")
            $features = [int]$TopologyRefs.ProvinceStats.FeatureCount
            Assert-Condition -Condition ($source -eq $features) -Message (
                $Case.Name + ": source_count " + $source + "!= Features " + $features +
                "++sourceCount")
            # 跨动作对撞（本刀信息量最大的一条）：multipart CSV 的 part_count 合计 == singlepart 展开后的部件数。
            # 成立前提"multipart 把每个要素都列了出来"已在上面钉死；而两个动作**各自独立**数部件
            # （geometryPartCount 读原始几何 / collectSinglepartGeometries 读归一化后的几何）
            # ⇒ 不相等就说明 MakeValid 改掉了部件数或有一处漏数，两个方向都能红。现读 588 == 588。
            Assert-Condition -Condition ($feature -eq $script:C3baMultipartProvParts) -Message (
                $Case.Name + ":" + $feature + "!= multipart_check part_count " +
                $script:C3baMultipartProvParts + " ")
            # 展开真的发生了（恒等 = 动作没干活）；与上一条不重复：那条核"多少"，这条核"变没变"。
            Assert-Condition -Condition ($feature -gt $source) -Message (
                $Case.Name + ": feature_count " + $feature + " source_count " + $source +
                " ")
            # skipped_count 在这份数据上实测 0，但**不钉** "== 0"：工单 C3b-4 刚为同一个形状消掉一处
            # "期望值本身不承重"（删掉 ++skippedCount 也是 0）。真实腿改核它参与的记账不等式：
            # 每个未跳过的要素至少产出 1 个部件 ⇒ feature >= source - skipped；
            # 0 与 1 两个方向的定义性覆盖在 C3b-4 的合成夹具里（那里钉 skipped_count == 1）。
            Assert-Condition -Condition ($feature -ge ($source - $skipped)) -Message (
                $Case.Name + ": feature_count " + $feature + "< source_count " + $source +
                "- skipped_count " + $skipped + " 1")
            # 图层声明必须从输入的 Unknown/变宽形态降到单部件类型（工单表格：图层名沿用输入、FID 由驱动重排）。
            Assert-Condition -Condition ($read.Geometry -ceq "Polygon") -Message (
                $Case.Name + ":" + $read.Geometry + " singlepart Multi")
        }
        "multipart_check_buf" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["multipart_buf_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,geom_type,part_count" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            $scanned = Get-ReportedMetadataCount -Text $Case.Output -Key "scanned_count" -Label ($Case.Name + " scanned_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            # 与 prov 臂同形的关系式，但样本从"42 个混合要素"换成"212 个纯面要素" ⇒ 不是给某份数据标定的常量。
            $features = [int]$TopologyRefs.BufferStats.FeatureCount
            Assert-Condition -Condition ($issue -eq $scanned -and $issue -eq $features) -Message (
                $Case.Name + ": issue_count " + $issue + "/ scanned_count " + $scanned +
                "/ Features " + $features + " ")
            # 每行打印的是**要素级**几何类型，而 vector info 现读的是**图层级**声明；
            # 两个独立读法在纯 MultiPolygon 缓冲层上必须给出同一个字符串（一旦有要素不是 Multi 形态、
            # 或实现改成打印图层声明，这条就红）。
            $types = @(Get-CsvColumn -Table $table -Name "geom_type" -Label $Case.Name)
            $distinct = @($types | Sort-Object -Unique)
            Assert-Condition -Condition ($distinct.Count -eq 1 -and $distinct[0] -ceq $TopologyRefs.BufferStats.Geometry) -Message (
                $Case.Name + ": CSV geom_type " + ($distinct -join "/") +
                " " + $TopologyRefs.BufferStats.Geometry)
        }
        "hole_check_prov" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["hole_prov_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,geom_type,hole_count" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            $scanned = Get-ReportedMetadataCount -Text $Case.Output -Key "scanned_count" -Label ($Case.Name + " scanned_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            $features = [int]$TopologyRefs.ProvinceStats.FeatureCount
            Assert-Condition -Condition ($scanned -eq $features) -Message (
                $Case.Name + ": scanned_count " + $scanned + "!= Features " + $features)
            # 口径"issue_count = 有洞的要素数、不是洞总数"（工单表格点名这条最容易写错）用两条独立关系钉死：
            # ① 每个 source_fid 在 CSV 里只出现一次 ⇒ 行是**按要素**组织的；若实现改成"每个洞写一行"，
            #    行数与 issue_count 会一起变成洞总数，①先红；
            # ② hole_count 合计 > 行数 ⇒ 这份数据里至少一个要素带 2 个以上内环，于是"把 issue_count
            #    改成累加 holeCount"（C3b-2 的 M-5a）在真实腿上也会红。现读 6 行、合计 10。
            $fids = @(Get-CsvColumn -Table $table -Name "source_fid" -Label $Case.Name)
            $distinct = @($fids | Sort-Object -Unique)
            Assert-Condition -Condition ($distinct.Count -eq $table.DataRowCount) -Message (
                $Case.Name + ": source_fid " + $distinct.Count + "!=" + $table.DataRowCount +
                " ")
            $counts = @(Get-CsvColumn -Table $table -Name "hole_count" -Label $Case.Name)
            $holeSum = 0
            foreach ($value in $counts) {
                $count = Get-CsvInt64 -Value $value -Label ($Case.Name + " hole_count")
                Assert-Condition -Condition ($count -ge 1) -Message (
                    $Case.Name + ": hole_count " + $count + "< 1 holeCount > 0")
                $holeSum += $count
            }
            Assert-Condition -Condition ($issue -ge 1) -Message (
                $Case.Name + ": /" + $issue)
            Assert-Condition -Condition ($holeSum -gt $issue) -Message (
                $Case.Name + ": hole_count " + $holeSum + " " + $issue +
                "' ' ' '")
            $script:C3baHoleProvIssue = $issue
        }
        "hole_check_buf" {
            Assert-Condition -Condition ($script:C3baHoleProvIssue -ge 0) -Message (
                $Case.Name + " ran before hole_check_prov, cannot cross-check the hole counts")
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["hole_buf_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,geom_type,hole_count" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            $scanned = Get-ReportedMetadataCount -Text $Case.Output -Key "scanned_count" -Label ($Case.Name + " scanned_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            $features = [int]$TopologyRefs.BufferStats.FeatureCount
            Assert-Condition -Condition ($scanned -eq $features) -Message (
                $Case.Name + ": scanned_count " + $scanned + "!= Features " + $features)
            # 成对写（工单："成对才非平凡"）：同一动作喂按行政边界拼合的省界 ⇒ 有洞要素 >= 1，
            # 喂 212 条道路缓冲 ⇒ 一个都没有（细长条带围不出内环）。两个方向各钉一次：
            # "恒报 0"被 prov 臂抓，"恒报正数"被本臂抓。
            Assert-Condition -Condition ($issue -eq 0) -Message (
                $Case.Name + ":" + $issue + " " +
                $script:C3baHoleProvIssue + " 0 prov >0")
        }
        "overlap_check_buf" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["overlap_buf_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,target_fid,overlap_area" -Label $Case.Name
            $relations = Get-ReportedMetadataCount -Text $Case.Output -Key "relation_count" -Label ($Case.Name + " relation_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $relations -Label $Case.Name
            Assert-Condition -Condition ($relations -ge 1) -Message (
                $Case.Name + ": 212 30 m overlap_check " + $relations)
            Assert-CsvFidPairsAscending -Table $table -Label $Case.Name
            # 实现里只在 overlapArea > 0 时写行 ⇒ "写出来的面积必然 > 0"是定义性判据；面积列拆列解析
            # （不是 find(",50") 那种子串），并要求面积取值不止一种 ⇒ "把面积写死成常量"这类变异
            # 在真实腿上必然红（现读最小 14.49、最大 3.08e6，跨五个数量级）。
            $areas = @(Get-CsvColumn -Table $table -Name "overlap_area" -Label $Case.Name)
            $worst = [double]::PositiveInfinity
            foreach ($area in $areas) {
                $value = Get-CsvDouble -Value $area -Label ($Case.Name + " overlap_area")
                Assert-Condition -Condition ($value -gt 0.0) -Message (
                    $Case.Name + ": overlap_area " + $value + "<= 0 <= 0")
                if ($value -lt $worst) { $worst = $value }
            }
            $distinct = @($areas | Sort-Object -Unique)
            Assert-Condition -Condition ($distinct.Count -gt 1) -Message (
                $Case.Name + ":" + $distinct.Count + " " + $worst +
                "' '")
            $script:C3baOverlapBufRelations = $relations
        }
        "topology_check_buf" {
            Assert-Condition -Condition ($script:C3baOverlapBufRelations -ge 0) -Message (
                $Case.Name + " ran before overlap_check_buf, cannot cross-check relation counts")
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["topology_buf_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,target_fid,issue_type,issue_value" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            $pairRows = 0
            Assert-TopologyIssueCsvShape -Table $table -PairRowCount ([ref]$pairRows) -Label $Case.Name
            # 跨动作一致性（本刀最硬的一条）：同一份输入、同一套 i<j 双层循环，
            # overlap_check 对每对写行当且仅当交面积 > 0；topology_check 对同一对要么走 Equals 分支写
            # duplicate_geometry（此时交面积必然也 > 0），要么走 else-if 写 overlap，两分支互斥 ⇒
            # "成对行数" == overlap_check 的关系数，另加 empty/invalid 两类**不成对**的行。
            # 两动作各自实现这套扫描 ⇒ 这条等式是两个独立实现的交叉验证，不是一处断言的复读。
            Assert-Condition -Condition ($pairRows -eq $script:C3baOverlapBufRelations) -Message (
                $Case.Name + ":" + $pairRows + "!= overlap_check " +
                $script:C3baOverlapBufRelations + " buf30 O(n )")
        }
        "overlap_check_prov" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["overlap_prov_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,target_fid,overlap_area" -Label $Case.Name
            $relations = Get-ReportedMetadataCount -Text $Case.Output -Key "relation_count" -Label ($Case.Name + " relation_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $relations -Label $Case.Name
            # 工单指定的三条之一（C3b-A 原文）：省层是**按行政边界拼合**的，面与面只共边不共面积 ⇒ 恒 0。
            # ⚠ 这条"恒 0"已被 C3d 实测推翻，而推翻它的正是 N125 本身：这份拼合层里 fid 12 与 fid 14 有
            #   401,282 m² 的**真实共面积**（topology_check 一直报得出来），旧实现只是把混合维度的交集
            #   算成 0 ⇒ 那一行根本没写。所以退役的不是一条判据，而是"把产品缺陷当成数据性质"的契约。
            #   被替换掉的旧断言原文登记在此，便于追溯：
            #     Assert-Condition -Condition ($relations -eq 0)   # 修复前实测 relations = 0
            #   须知第 5 条说"不改老 case 的名字或语义"——用例名 overlap_check_prov、它读的 CSV、
            #   表头整串核对、"行数 == 报告计数"全部原样保留；只有这条 "== 0" 被事实本身推翻。
            # 仍然成对读：overlap_check(buf30) >= 1 与 adjacency(buf30) == 0、
            # adjacency(prov3857) >= 1 与 overlap_check(prov3857) >= 1 —— 同一批动作、两份数据，
            # "共边"与"共面积"两个谓词各自承重，任何一方"恒 0"或"恒正"都会被另一臂抓住。
            # C3d 新增（工单"互证"第一条）：混合维度交集不得再被漏报 ⇒ 省界 relation_count >= 1。
            Assert-Condition -Condition ($relations -ge 1) -Message (
                $Case.Name + ": relation_count=" + $relations +
                " expected >= 1 after N125 fix (buf30 relation_count=" +
                $script:C3baOverlapBufRelations + ")")
            Assert-Condition -Condition ($script:C3baOverlapBufRelations -ge 1) -Message (
                $Case.Name + ": overlap buf30 > 0 " +
                $script:C3baOverlapBufRelations)
            $script:C3baOverlapProvRelations = $relations
        }
        "topology_check_prov" {
            Assert-Condition -Condition ($script:C3baOverlapProvRelations -ge 0) -Message (
                $Case.Name + " ran before overlap_check_prov, cannot cross-check relation counts")
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["topology_prov_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,target_fid,issue_type,issue_value" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            $pairRows = 0
            Assert-TopologyIssueCsvShape -Table $table -PairRowCount ([ref]$pairRows) -Label $Case.Name
            # 9d98497 写的单向关系 ">=" **保持原样不动**（工单 C3d 须知第 5 条：老 case 的 ">=" 写法要可追溯）。
            # C3b-A 当时之所以只敢写单向，是因为省界臂实测 overlap 报 0 条而 topology 报 1 条 —— 那是 N125，
            # 当时写成 "==" 就等于把产品缺陷钉成契约。C3d 把两个动作的交面积收成单源后，等式才第一次被允许写。
            Assert-Condition -Condition ($pairRows -ge $script:C3baOverlapProvRelations) -Message (
                $Case.Name + ":" + $pairRows + "< overlap_check " +
                $script:C3baOverlapProvRelations + " overlap topology")
            # C3d 新增（工单"互证"第二条：把省界臂收紧成等式，另开一条、不改上面那条）：
            # 修好后两个动作是同一套 i<j 扫描 + 同一个 polygonIntersectionArea() ⇒ 对每一对要素，
            # topology 的成对行 = duplicate_geometry 行 + overlap 行；Equals 为真的那对交集就是它自己、
            # 面积必然 > 0 ⇒ overlap_check 对同一对也写行 ⇒ 成对行数 == relation_count。
            # buf30 臂早就这么写（那里两个动作本来同源），省界臂此前只能写 ">="。这条一红就说明
            # 有人又把交面积口径拆成了两份（或把 GeometryCollection 那支改回了 default 0）。
            Assert-Condition -Condition ($pairRows -eq $script:C3baOverlapProvRelations) -Message (
                $Case.Name + ": pairRows=" + $pairRows + " != overlap_check relation_count=" +
                $script:C3baOverlapProvRelations + " (C3d cross-check on prov3857, N125 closed)")
            # 真实省界不是严丝合缝的：这份拼合层至少报出一个拓扑问题（现读 1 条 GeometryCollection 型重叠）。
            Assert-Condition -Condition ($issue -ge 1) -Message (
                $Case.Name + ":" + $issue + " adjacency " +
                " ")
        }
        "adjacency_prov" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["adjacency_prov_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,target_fid,shared_length" -Label $Case.Name
            $relations = Get-ReportedMetadataCount -Text $Case.Output -Key "relation_count" -Label ($Case.Name + " relation_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $relations -Label $Case.Name
            Assert-Condition -Condition ($relations -ge 1) -Message (
                $Case.Name + ": 42 " + $relations + " ")
            Assert-CsvFidPairsAscending -Table $table -Label $Case.Name
            $lengths = @(Get-CsvColumn -Table $table -Name "shared_length" -Label $Case.Name)
            $zeros = 0
            foreach ($length in $lengths) {
                $value = Get-CsvDouble -Value $length -Label ($Case.Name + " shared_length")
                # 定义性：sharedBoundaryLength 是交集的线长度，不可能为负；负值只能是把别的量写错了列。
                Assert-Condition -Condition ($value -ge 0.0) -Message (
                    $Case.Name + ": shared_length " + $value + "< 0")
                if ($value -eq 0.0) { $zeros++ }
            }
            # 点接触也写行（C3b-B1 在合成夹具上新增的那条语义，在真实省界上同样存在）：
            # 现读 70 行里恰有 1 行 0.0 ⇒ "只在有长度时才写行"（M-2a 那一族）被 zeros >= 1 抓，
            # "长度算式塌成常量"被 zeros < 数据行 抓。两个条件都是拆列后逐行数出来的。
            Assert-Condition -Condition ($zeros -ge 1 -and $zeros -lt $table.DataRowCount) -Message (
                $Case.Name + ": shared_length 0 " + $zeros + " " + $table.DataRowCount +
                "' (0.0) 0'")
            $script:C3baAdjacencyProvRelations = $relations
        }
        "adjacency_buf" {
            Assert-Condition -Condition ($script:C3baOverlapBufRelations -ge 0) -Message (
                $Case.Name + " ran before overlap_check_buf, cannot cross-check relation counts")
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["adjacency_buf_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,target_fid,shared_length" -Label $Case.Name
            $relations = Get-ReportedMetadataCount -Text $Case.Output -Key "relation_count" -Label ($Case.Name + " relation_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $relations -Label $Case.Name
            # 工单三条的最后一半：同一份 buf30 上 overlap_check 报 >= 1 对，而 adjacency 报 0 ——
            # 缓冲带互相**压叠**（有面积）而不是只接触，Touches 谓词不成立。
            # 与 prov3857 臂（adjacency >= 1 / overlap == 0）合起来 ⇒ 两个动作各自的谓词在两份数据上都承重。
            Assert-Condition -Condition ($relations -eq 0) -Message (
                $Case.Name + ":" + $relations + " overlap " +
                $script:C3baOverlapBufRelations + " ")
            Assert-Condition -Condition ($script:C3baAdjacencyProvRelations -ge 1) -Message (
                $Case.Name + ": adjacency_prov > 0 == 0 " +
                $script:C3baAdjacencyProvRelations)
        }
        "dangling_endpoint_w212" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["dangling_window_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,endpoint_type,x,y" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            $scanned = Get-ReportedMetadataCount -Text $Case.Output -Key "scanned_count" -Label ($Case.Name + " scanned_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            $features = [int]$TopologyRefs.WindowStats.FeatureCount
            Assert-Condition -Condition ($scanned -eq $features) -Message (
                $Case.Name + ": scanned_count " + $scanned + "!= Features " + $features)
            # 真实路网被 20 km 窗切断 ⇒ 必有悬空端点；与闭合环臂的"恒 0"成对。
            Assert-Condition -Condition ($issue -ge 1) -Message (
                $Case.Name + ":" + $issue + " ")
            # endpoint_type 列名像文本、实为 0=start / 1=end（CHANGELOG 已记的口径，别再靠猜）。
            $types = @(Get-CsvColumn -Table $table -Name "endpoint_type" -Label $Case.Name)
            $starts = 0
            $ends = 0
            foreach ($type in $types) {
                $code = Get-CsvInt64 -Value $type -Label ($Case.Name + " endpoint_type")
                Assert-Condition -Condition ($code -eq 0 -or $code -eq 1) -Message (
                    $Case.Name + ": endpoint_type '" + $type + "' 0=start / 1=end")
                if ($code -eq 0) { $starts++ } else { $ends++ }
            }
            # 两个类型都出现（现读 29 start / 23 end）⇒ "类型恒写 0" 在真实腿上也会红；
            # 定义性的一侧（每个端点各占一行、类型逐行对号）由 B1 的四条线夹具钉住。
            Assert-Condition -Condition ($starts -ge 1 -and $ends -ge 1 -and ($starts + $ends) -eq $table.DataRowCount) -Message (
                $Case.Name + ": start " + $starts + " end " + $ends + " " + $table.DataRowCount)
            Assert-CsvPointColumnsInsideExtent -Table $table -XColumn "x" -YColumn "y" `
                -Stats $TopologyRefs.WindowStats -Tolerance 0.01 -Label $Case.Name
            $script:C3baDanglingWindowIssue = $issue
        }
        "dangling_endpoint_closed_ring" {
            Assert-Condition -Condition ($script:C3baDanglingWindowIssue -ge 0) -Message (
                $Case.Name + " ran before dangling_endpoint_w212, cannot cross-check dangling counts")
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["dangling_ring_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,endpoint_type,x,y" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            $scanned = Get-ReportedMetadataCount -Text $Case.Output -Key "scanned_count" -Label ($Case.Name + " scanned_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            $features = [int]$TopologyRefs.BoundaryStats.FeatureCount
            Assert-Condition -Condition ($scanned -eq $features) -Message (
                $Case.Name + ": scanned_count " + $scanned + "!= Features " + $features)
            # 定义性（不是"我猜 0"）：闭合环的 StartPoint 与 EndPoint 同坐标 ⇒ 每个端点键至少出现 2 次
            # ⇒ 悬空必然为 0。与路网臂（>= 1）成对 ⇒ "把所有端点都当悬空"和"issue_count 写成常量 0"
            # 两个方向各被钉一次。这就是本刀要多造一个 boundary 派生件的全部理由。
            Assert-Condition -Condition ($issue -eq 0) -Message (
                $Case.Name + ":" + $issue + " 0 " +
                " " + $script:C3baDanglingWindowIssue + " ")
            Assert-Condition -Condition ($script:C3baDanglingWindowIssue -ge 1) -Message (
                $Case.Name + ": > 0")
        }
        "duplicate_point_ring_endpoints" {
            $table = Get-CsvTable -Path ([string]$TopologyRefs.Outputs["duplicate_ring_csv"])
            Assert-CsvHeader -Table $table -Expected "source_fid,duplicate_fid,x,y" -Label $Case.Name
            $issue = Get-ReportedMetadataCount -Text $Case.Output -Key "issue_count" -Label ($Case.Name + " issue_count")
            $scanned = Get-ReportedMetadataCount -Text $Case.Output -Key "scanned_count" -Label ($Case.Name + " scanned_count")
            $skipped = Get-ReportedMetadataCount -Text $Case.Output -Key "skipped_geometry_count" -Label ($Case.Name + " skipped_geometry_count")
            Assert-CsvRowsEqualReported -Table $table -Reported $issue -Label $Case.Name
            $features = [int]$TopologyRefs.EndpointStats.FeatureCount
            Assert-Condition -Condition ($scanned -eq $features) -Message (
                $Case.Name + ": scanned_count " + $scanned + "!= Features " + $features)
            Assert-Condition -Condition ($issue -ge 1) -Message (
                $Case.Name + ":" + ($features / 2) + " 2")
            # 重复对数由**构造**给出下界（算式见文件头）：环数 = Features/2（endpoints_extract 对每条线
            # 恰好写首末 2 个点），每环那一对逐字节重合 ⇒ 去重键数 <= Features/2，
            # 而 issue = Σ(n_k-1) = Features - 去重键数 >= Features/2。这条与样本无关：
            # 换成任何 boundary→endpoints 的真实数据都成立，而"每环只写一个点""漏写第二对"会当场红。
            # 现读 2 × 570 = 1140 >= 1134。
            Assert-Condition -Condition (2 * $issue -ge $scanned) -Message (
                $Case.Name + ": 2 * issue_count(" + $issue + ") < scanned_count(" + $scanned +
                ") ' '")
            # skipped_geometry_count == 0 在真实腿上是**定义性**的：输入是 endpoints_extract 以 wkbPoint
            # 建层、每条要素都是 OGRPoint ⇒ 实现里"非点几何"分支不可能进。
            # （C3b-B2 在合成夹具上把同一个键钉成 "1"，两批合起来 0 与 1 两个方向都有覆盖，
            #   与 C3b-4 说的"期望值为 0 不承重"不冲突：那条讲的是**唯一**一臂，这里是跨动作的必然。）
            Assert-Condition -Condition ($skipped -eq 0) -Message (
                $Case.Name + ": skipped_geometry_count " + $skipped + " 0 " +
                $TopologyRefs.EndpointStats.Geometry + " OGRPoint")
            Assert-CsvFidPairsAscending -Table $table -Label $Case.Name
            # (source_fid, duplicate_fid) 逐对唯一：输入每条要素恰好 1 个点 ⇒ 同一对不可能写两次
            # （实现是"首次出现的 FID + 当前 FID"）。重复的 FID 对只能来自重复写入。
            $pairKeys = @()
            for ($i = 0; $i -lt $table.DataRowCount; $i++) {
                $row = @($table.Rows[$i])
                $pairKeys += ([string]$row[0] + "," + [string]$row[1])
            }
            $distinctPairs = @($pairKeys | Sort-Object -Unique)
            Assert-Condition -Condition ($distinctPairs.Count -eq $table.DataRowCount) -Message (
                $Case.Name + ": FID " + $distinctPairs.Count + "!=" + $table.DataRowCount + " ")
            Assert-CsvPointColumnsInsideExtent -Table $table -XColumn "x" -YColumn "y" `
                -Stats $TopologyRefs.EndpointStats -Tolerance 0.01 -Label $Case.Name
        }
        "geom_metrics_buf" {
            # 输出图层声明是输入的 **Multi 变体**（resultLayerGeometryType），回读到的 Geometry 与输入不同名
            # 也可能同名（buf30 本来就是 MultiPolygon）⇒ 判据按契约写 "^Multi"，不照抄派生批的期望值。
            $read = Assert-DerivedGeometryCase -Name $Case.Name -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$TopologyRefs.Outputs["metrics_buf"]) -ScratchDir $TopologyRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $TopologyRefs.BufferStats `
                -ExpectedGeometryPattern "^Multi" -ExtentMode "equal" -FeaturesMode "equal"
            $feature = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label ($Case.Name + " feature_count")
            $features = [int]$TopologyRefs.BufferStats.FeatureCount
            # 只有 feature_count 一个键 ⇒ 记账只能与落盘对撞（上面已核 feature_count == 回读 == 输入）。
            Assert-Condition -Condition ($feature -eq $features) -Message (
                $Case.Name + ": feature_count " + $feature + "!= Features " + $features +
                " ")
            Assert-Condition -Condition ($read.Geometry -ceq "MultiPolygon") -Message (
                $Case.Name + ":" + $read.Geometry + " MultiPolygon Multi")
            # 字段契约：输入字段全量继承 + 5 个 OFTReal 指标字段，于是"输出字段数 == 输入字段数 + 5"
            # 是机器级关系；5 个名字与类型再从 info 逐条整行锚定。
            $inherited = $TopologyRefs.BufferStats.FieldCount
            Assert-Condition -Condition ($null -ne $inherited) -Message (
                $Case.Name + ": '+5'")
            Assert-Condition -Condition ($null -ne $read.FieldCount -and [int]$read.FieldCount -eq ([int]$inherited + 5)) -Message (
                $Case.Name + ":" + $read.FieldCount + "!=" + $inherited + "+ 5")
            $info = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "vector", "info",
                ("--input=" + [string]$TopologyRefs.Outputs["metrics_buf"]),
                ("--output=" + (Join-Path $TopologyRefs.ScratchRoot "metrics_info_placeholder.gpkg"))
            )
            foreach ($name in @("geom_area", "geom_length", "compactness", "circularity", "orientation")) {
                Assert-Condition -Condition ([regex]::IsMatch($info, ("(?m)^\s*\[\d+\] " + $name + " \(Real\)\s*$"))) -Message (
                    $Case.Name + ": OFTReal " + $name)
            }
        }
        "sliver_remove_buf_low_area" {
            $read = Assert-DerivedGeometryCase -Name $Case.Name -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$TopologyRefs.Outputs["sliver_low"]) -ScratchDir $TopologyRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $TopologyRefs.BufferStats `
                -ExpectedGeometryPattern "^MultiPolygon$" -ExtentMode "equal" -FeaturesMode "equal"
            $feature = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label ($Case.Name + " feature_count")
            $source = Get-ReportedMetadataCount -Text $Case.Output -Key "source_count" -Label ($Case.Name + " source_count")
            $skipped = Get-ReportedMetadataCount -Text $Case.Output -Key "skipped_count" -Label ($Case.Name + " skipped_count")
            $removedParts = Get-ReportedMetadataCount -Text $Case.Output -Key "removed_part_count" -Label ($Case.Name + " removed_part_count")
            $removedFeatures = Get-ReportedMetadataCount -Text $Case.Output -Key "removed_feature_count" -Label ($Case.Name + " removed_feature_count")
            $features = [int]$TopologyRefs.BufferStats.FeatureCount
            Assert-Condition -Condition ($source -eq $features) -Message (
                $Case.Name + ": source_count " + $source + "!= Features " + $features)
            # 记账恒等式（三个分支互斥、每个要素必走其一）：留下的 + 整要素删掉的 + 空几何跳过的 == 扫过的。
            # 这是 sliver_remove 五个键里唯一能独立核账的关系 ⇒ 少记任何一个都红。两臂都写这一条。
            Assert-Condition -Condition ($feature + $removedFeatures + $skipped -eq $source) -Message (
                $Case.Name + ": feature_count " + $feature + "+ removed_feature_count " + $removedFeatures +
                "+ skipped_count " + $skipped + "!= source_count " + $source)
            Assert-Condition -Condition ($removedParts -ge $removedFeatures) -Message (
                $Case.Name + ": removed_part_count " + $removedParts + "< removed_feature_count " + $removedFeatures +
                "1")
            # 阈值取 1e-4 m²（1 cm²）比任何真实道路缓冲条带都小 ⇒ 这一臂是"阈值不起作用时的恒等基准"，
            # 与下一臂（阈值拉到框架区间闸的上界 1e9 m² = 1 km²）成对 ⇒ 单看本臂的 removed==0 不承重，
            # 成对之后"min_area 参数没用"必红（两臂同值）、"阈值方向写反"必红（高阈臂留下的更多）。
            Assert-Condition -Condition ($feature -eq $source -and $removedFeatures -eq 0) -Message (
                $Case.Name + ": 1e-4 m feature " + $feature + "/ source " + $source +
                "/ removed_feature " + $removedFeatures)
            $script:C3baSliverLowFeatures = $feature
        }
        "sliver_remove_buf_high_area" {
            Assert-Condition -Condition ($script:C3baSliverLowFeatures -ge 0) -Message (
                $Case.Name + " ran before sliver_remove_buf_low_area, cannot cross-check thresholds")
            $feature = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label ($Case.Name + " feature_count")
            $source = Get-ReportedMetadataCount -Text $Case.Output -Key "source_count" -Label ($Case.Name + " source_count")
            $skipped = Get-ReportedMetadataCount -Text $Case.Output -Key "skipped_count" -Label ($Case.Name + " skipped_count")
            $removedParts = Get-ReportedMetadataCount -Text $Case.Output -Key "removed_part_count" -Label ($Case.Name + " removed_part_count")
            $removedFeatures = Get-ReportedMetadataCount -Text $Case.Output -Key "removed_feature_count" -Label ($Case.Name + " removed_feature_count")
            $features = [int]$TopologyRefs.BufferStats.FeatureCount
            Assert-Condition -Condition ($source -eq $features) -Message (
                $Case.Name + ": source_count " + $source + "!= Features " + $features)
            Assert-Condition -Condition ($feature + $removedFeatures + $skipped -eq $source) -Message (
                $Case.Name + ": feature " + $feature + "+ removed_feature " + $removedFeatures +
                "+ skipped " + $skipped + "!= source " + $source)
            # 单调 + 真的咬到：阈值放大留下的只可能更少，而缓冲条带部件最大约 3.08e6 m² << 1e9 m² ⇒
            # 本臂必须严格少于低阈臂。现读 feature_count = 0、removed_feature_count = 212。
            Assert-Condition -Condition ($feature -lt $script:C3baSliverLowFeatures) -Message (
                $Case.Name + ": 1e9 m " + $feature + " " +
                $script:C3baSliverLowFeatures + " min_area")
            Assert-Condition -Condition ($removedFeatures -ge 1 -and $removedParts -ge $removedFeatures) -Message (
                $Case.Name + ": removed_feature " + $removedFeatures + "/ removed_part " + $removedParts)
            # 报数与落盘对撞；这一臂可能是空图层（vector info 不打印 Extent ⇒ 走只读 Features 的取数器）。
            $readback = Get-LayerFeatureCount -ResolvedCliPath $ResolvedCliPath `
                -Path ([string]$TopologyRefs.Outputs["sliver_high"]) -ScratchDir $TopologyRefs.ScratchRoot
            Assert-Condition -Condition ($readback -eq $feature) -Message (
                $Case.Name + ": feature_count " + $feature + " vector info " + $readback)
        }
        "nearest_w212_self" {
            $read = Assert-DerivedGeometryCase -Name $Case.Name -ResolvedCliPath $ResolvedCliPath `
                -OutputPath ([string]$TopologyRefs.Outputs["nearest_self"]) -ScratchDir $TopologyRefs.ScratchRoot `
                -CliOutput $Case.Output -InputStats $TopologyRefs.WindowStats `
                -ExpectedGeometryPattern "^MultiLineString$" -ExtentMode "equal" -FeaturesMode "equal"
            $feature = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label ($Case.Name + " feature_count")
            $features = [int]$TopologyRefs.WindowStats.FeatureCount
            # feature_count == 输入要素数 是对挂账 N124 的盯防：实现里"空源几何 continue 且不计数"，
            # 差额在产物里无处可查 ⇒ 真实腿先把"这份数据的每条线都有几何"证成事实，
            # 一旦上游产出空几何（或跳过分支被写宽）本条即红。
            Assert-Condition -Condition ($feature -eq $features) -Message (
                $Case.Name + ": feature_count " + $feature + "!= Features " + $features +
                "= N124")
            # nearest_vector 是**入参回显**（目标图层路径），不是"目标数"（工单表格点名这条最容易读错）。
            $echo = Get-ReportedMetadataValue -Text $Case.Output -Key "nearest_vector" -Label ($Case.Name + " nearest_vector")
            Assert-Condition -Condition ($echo -ceq [string]$TopologyRefs.LineWindow) -Message (
                $Case.Name + ": nearest_vector '" + $echo + "'")
            # sameDataset 语义（工单指定"用 w212 自己当目标"就是为了覆盖这条）：输入与目标是同一个文件
            # ⇒ 实现里 srcFeat->GetFID() == targetFeat->GetFID() 一律跳过 ⇒ 产物中不可能出现
            # nearest_fid == fid 的行。用 vector filter 的 where 子句把这条语义变成一个可整行锚定的数：
            # 现读 0；去掉跳过分支 ⇒ 每个要素到自己的距离 0 最小 ⇒ 变成 212 ⇒ 红。
            $selfText = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
                "vector", "filter",
                ("--input=" + [string]$TopologyRefs.Outputs["nearest_self"]),
                ("--output=" + (Join-Path $TopologyRefs.ScratchRoot "nearest_self_match.gpkg")),
                "--where=nearest_fid = fid"
            )
            $selfCount = Get-ReportedCount -Text $selfText `
                -Pattern "(?m)^Success: Filter completed: (\d+) features written" -Label ($Case.Name + "")
            Assert-Condition -Condition ($selfCount -eq 0) -Message (
                $Case.Name + ":" + $selfCount + " nearest_fid == fid sameDataset FID")
        }
        default {
            throw ($Case.Name + " C3b-A Assert-TopologyCheckCase")
        }
    }
}

function Validate-CaseOutputs {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [string]$ResolvedOutputRoot,
        [int]$InputFeatureCount,
        [object]$DerivedRefs,
        [object]$TopologyRefs
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
    # C3b-A 的腿判据走独立函数（同一批腿互相要用现读数；名单即门禁，见函数头）。
    # 不塞进上面那个已经 30 多个分支的 switch，免得两个批次的判据互相遮蔽。
    # 不在名单里的用例名直接 return ⇒ 上面那批（C3a 等）行为与改动前逐字一致。
    Assert-TopologyCheckCase -Case $Case -ResolvedCliPath $ResolvedCliPath -TopologyRefs $TopologyRefs
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

# quick 档不造这批派生件（工单：11 条腿归 real_data_vector_full 那一档）⇒ 先置 $null，
# 让"用例被单独抽出来跑"落在 Assert-TopologyCheckCase 的显式判红上，而不是变量不存在。
$topologyRefs = $null

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
    # ── C3b-A：vector 拓扑/检查批 11 动作的真实数据腿（18 条用例；归 real_data_vector_full 那一档）──
    # 派生件与判据的算式都写在 Ensure-TopologyCheckInputs / Assert-TopologyCheckCase 的注释块里。
    $topologyRefs = Ensure-TopologyCheckInputs -ResolvedCliPath $ResolvedCliPath `
        -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -RoadsCore3857 $roadsCore3857
    $c3baOutputs = $topologyRefs.Outputs

    $cases += New-CheckCase -Name "multipart_check_prov" -Action "multipart_check" `
        -InputPath $topologyRefs.ProvinceProjected -OutputPath ([string]$c3baOutputs["multipart_prov_csv"])
    # 顺序即依赖：singlepart 的部件数要与上面 multipart CSV 的 part_count 合计对撞。
    $cases += New-CheckCase -Name "singlepart_prov" -Action "singlepart" `
        -InputPath $topologyRefs.ProvinceProjected -OutputPath ([string]$c3baOutputs["singlepart_prov"])
    $cases += New-CheckCase -Name "multipart_check_buf" -Action "multipart_check" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["multipart_buf_csv"])

    $cases += New-CheckCase -Name "hole_check_prov" -Action "hole_check" `
        -InputPath $topologyRefs.ProvinceProjected -OutputPath ([string]$c3baOutputs["hole_prov_csv"])
    $cases += New-CheckCase -Name "hole_check_buf" -Action "hole_check" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["hole_buf_csv"])

    # 同一份 buf30 先跑 overlap 再跑 topology：topology 臂要拿成对行数与 overlap 的关系数对撞。
    $cases += New-CheckCase -Name "overlap_check_buf" -Action "overlap_check" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["overlap_buf_csv"])
    $cases += New-CheckCase -Name "topology_check_buf" -Action "topology_check" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["topology_buf_csv"])
    $cases += New-CheckCase -Name "overlap_check_prov" -Action "overlap_check" `
        -InputPath $topologyRefs.ProvinceProjected -OutputPath ([string]$c3baOutputs["overlap_prov_csv"])
    $cases += New-CheckCase -Name "topology_check_prov" -Action "topology_check" `
        -InputPath $topologyRefs.ProvinceProjected -OutputPath ([string]$c3baOutputs["topology_prov_csv"])

    $cases += New-CheckCase -Name "adjacency_prov" -Action "adjacency" `
        -InputPath $topologyRefs.ProvinceProjected -OutputPath ([string]$c3baOutputs["adjacency_prov_csv"])
    $cases += New-CheckCase -Name "adjacency_buf" -Action "adjacency" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["adjacency_buf_csv"])

    $cases += New-CheckCase -Name "dangling_endpoint_w212" -Action "dangling_endpoint_check" `
        -InputPath $topologyRefs.LineWindow -OutputPath ([string]$c3baOutputs["dangling_window_csv"])
    $cases += New-CheckCase -Name "dangling_endpoint_closed_ring" -Action "dangling_endpoint_check" `
        -InputPath $topologyRefs.ProvinceBoundary -OutputPath ([string]$c3baOutputs["dangling_ring_csv"])

    $cases += New-CheckCase -Name "duplicate_point_ring_endpoints" -Action "duplicate_point_check" `
        -InputPath $topologyRefs.RingEndpoints -OutputPath ([string]$c3baOutputs["duplicate_ring_csv"])

    $cases += New-CheckCase -Name "geom_metrics_buf" -Action "geom_metrics" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["metrics_buf"])

    # 两条阈值臂必须同批跑：低阈臂给"恒等基准"，高阈臂给"参数真生效"。
    # 1e9 是框架参数区间闸允许的上界（现读报错文案 [0, 1e+09]）。
    $cases += New-CheckCase -Name "sliver_remove_buf_low_area" -Action "sliver_remove" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["sliver_low"]) `
        -ExtraArgs @("--min_area=0.0001")
    $cases += New-CheckCase -Name "sliver_remove_buf_high_area" -Action "sliver_remove" `
        -InputPath $topologyRefs.BufferPolygons -OutputPath ([string]$c3baOutputs["sliver_high"]) `
        -ExtraArgs @("--min_area=1000000000")

    # nearest 的两个输入都用 w212（同 CRS、同数据集 ⇒ sameDataset 跳过同 FID 这条语义正好有真实腿覆盖）。
    $cases += New-CheckCase -Name "nearest_w212_self" -Action "nearest" `
        -InputPath $topologyRefs.LineWindow -OutputPath ([string]$c3baOutputs["nearest_self"]) `
        -ExtraArgs @(("--nearest_vector=" + $topologyRefs.LineWindow))
}

# C3b-A 的跨臂现读数：全部初始化为 -1（"还没跑"），消费侧的守卫判 -ge 0 ⇒
# 用例被重新排序、或某条腿在别的档里被跳过时，红的是"跑的顺序不对"，而不是拿上一轮的旧值蒙过去。
$script:C3baMultipartProvParts = -1
$script:C3baHoleProvIssue = -1
$script:C3baOverlapBufRelations = -1
$script:C3baOverlapProvRelations = -1
$script:C3baAdjacencyProvRelations = -1
$script:C3baDanglingWindowIssue = -1
$script:C3baSliverLowFeatures = -1

# 名单与注册表对撞（正向）：名单里的每一条都必须真的注册成用例；quick 档整批不注册 ⇒ 只在 full 档核条数。
# 反向的"进了名单却没有判据分支"由 Assert-TopologyCheckCase 的 default 负责，两侧都堵住才是闭环。
if ($Mode -eq "full") {
    $registered = @($cases | Where-Object { $script:TopologyCheckCaseNames -contains $_.Name })
    Assert-Condition -Condition ($registered.Count -eq $script:TopologyCheckCaseNames.Count) -Message (
        "C3b-A roster has " + $script:TopologyCheckCaseNames.Count + " name(s), but " + $registered.Count + " case(s) got registered")
}

$results = @()
foreach ($case in $cases) {
    $result = Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
    Validate-CaseOutputs -Case $result -ResolvedCliPath $ResolvedCliPath -ResolvedOutputRoot $ResolvedOutputRoot -InputFeatureCount $InputFeatureCount -DerivedRefs $derivedRefs -TopologyRefs $topologyRefs
    $results += $result
}

Save-RegressionResults -ResolvedOutputRoot $ResolvedOutputRoot -Mode $Mode -Results $results

Write-Host "Vector regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(20) + " " + $result.Seconds + "s")
}
Write-Host ("  summary_json".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".json")))
Write-Host ("  summary_csv".PadRight(22) + (Join-Path $ResolvedOutputRoot ("summary_" + $Mode + ".csv")))
