param(
    [string]$CliPath = "",
    [string]$WorkspaceRoot = "",
    [string]$OutputRoot = "",
    [ValidateSet("quick", "full")]
    [string]$Mode = "quick",
    # Group 按"消费哪批派生件"划分五条真实腿（见 GIS_CASE_ROSTER 那段注释）：
    # checks = C3b-A 的拓扑/检查批；overlay_buffer_join = buffer / spatial_join / 几何派生与转换小用例；
    # overlay_ops = clip/difference/intersect/union 的 focus/far/partial/stress 全族；
    # union_poly = C3h-3 的 union 路径 A 腿（C3k-2 把原 overlay 一档按"重活一分为二"拆成前两组）；
    # derive_heavy = C3c-B 起的"单条就几十秒"的几何派生重活档（现只有 simplify_roads）。
    # 取 ValidateSet 而不是自由字符串：-GROUP 写错时要在参数绑定期就红，
    # 不许顺着下面的注册走到"一组都没匹配上 => 0 条用例 => 退出码 0"的假绿。
    [ValidateSet("all", "checks", "overlay_buffer_join", "overlay_ops", "union_poly", "derive_heavy")]
    [string]$Group = "all"
)

$ErrorActionPreference = "Stop"
if ($null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)) {
    $PSNativeCommandUseErrorActionPreference = $false
}

# quick 档不分档（27 条用例各档都跑，Group=all）：-GROUP 写成 checks/overlay_buffer_join/overlay_ops/
# union_poly/derive_heavy 配 Mode=quick 会筛出 0 条用例 => 整条腿"退出码 0 但什么都没判"，所以这里先判红。放在最顶上是为了在碰 data/ 与
# tmp/ 之前就红（同 tests/run_real_vector_regression.cmake 里"参数写错要在绑定期红"那条口径）。
if ($Mode -eq "quick" -and $Group -ne "all") {
    throw ("Mode=quick has no group split: pass -Group all (got Mode=" + $Mode + " Group=" + $Group + ")")
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
        [string[]]$ExpectedOutputs,
        # Group 不设默认值：注册一条用例却忘了归组，必须由下面那条名单不变量点名判红，
        # 不许"默认进某一档"把漏归组洗成静默的档内缺席。
        [string]$Group
    )

    return [pscustomobject]@{
        Name = $Name
        Args = [string[]]$CaseArgs
        ExpectedOutputs = [string[]]$ExpectedOutputs
        Group = $Group
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
        # Group 必须带进结果对象：Validate-CaseOutputs 的 union_poly 分发按组标走（不按用例名），
        #   少了这一行那条分发永远不成立 ⇒ 整条腿"跑了但没人判"，而名单不变量照样绿（本刀实测踩过）。
        Group = $Case.Group
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
        [string]$JsonPath,
        [string]$CsvPath,
        [string]$Mode,
        [string]$Group,
        [object[]]$Results
    )

    $payload = [pscustomobject]@{
        mode = $Mode
        group = $Group
        generated_at = (Get-Date).ToString("s")
        output_root = (Split-Path $JsonPath -Parent)
        results = $Results
    }

    $payload | ConvertTo-Json -Depth 6 | Set-Content -Path $JsonPath -Encoding UTF8
    $Results | Select-Object Name,ExitCode,Seconds | Export-Csv -Path $CsvPath -NoTypeInformation -Encoding UTF8
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

# Assert-CountEquals 已随本刀的取数口统一消失：它的 10 个调用点全部改走
# Assert-ReportedMetadataCountEquals / Get-ReportedMetadataCount（整行锚定的自报键），
# 另有 dissolve_bbox 与 polygonize_bbox_raster 两条从 Assert-TextContains 的散文整串换成同一个键；
# 仓库内再没有调用者 ⇒ 按 C3h-2b 的死代码口径直接删掉，不留"以后可能用"的空壳。

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

# ── C3c-A：info 输出的结构化解析（Driver / 层数 / 层名 / 几何 / CRS / 要素数 / 字段名与顺序 / Extent）──
# Get-LayerStats 只带要素数/字段数/几何/包框；C3c-A 的 info 腿要按"整层形状"判，
# 所以另立一个解析器，而**不**扩宽既有 helper 的返回对象（那条被十几条腿消费，加属性要逐个复核）。
# 数字一律走与 Get-LayerStats 相同的整行锚定正则（禁止 Contains 判数字，历史假绿见那边的注释）。
function Get-LayerInfoSummary {
    param(
        [string]$Text,
        [string]$Label
    )

    $number = "-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?"
    $driver = [regex]::Match($Text, "(?m)^\s*Driver:\s+(.+?)\s*$")
    Assert-Condition -Condition ($driver.Success) -Message ($Label + ": no Driver line in info output: " + $Text)
    $layers = [regex]::Match($Text, "(?m)^\s*Layers:\s+(\d+)\s*$")
    Assert-Condition -Condition ($layers.Success) -Message ($Label + ": no Layers line in info output: " + $Text)
    $layerName = [regex]::Match($Text, "(?m)^\s*Layer \[\d+\]:\s+(.+?)\s*$")
    Assert-Condition -Condition ($layerName.Success) -Message ($Label + ": no 'Layer [i]: name' line in info output: " + $Text)
    $geometry = [regex]::Match($Text, "(?m)^\s*Geometry:\s+(\S+)\s*$")
    Assert-Condition -Condition ($geometry.Success) -Message ($Label + ": no Geometry line in info output: " + $Text)
    $crs = [regex]::Match($Text, "(?m)^\s*CRS:\s+(.+?)\s*$")
    Assert-Condition -Condition ($crs.Success) -Message ($Label + ": no CRS line in info output: " + $Text)
    $features = [regex]::Match($Text, "(?m)^\s*Features:\s+(\d+)\s*$")
    Assert-Condition -Condition ($features.Success) -Message ($Label + ": no Features line in info output: " + $Text)
    $extent = [regex]::Match($Text, ("(?m)^\s*Extent:\s+(" + $number + "),\s*(" + $number + ")\s+-\s*(" + $number + "),\s*(" + $number + ")\s*$"))
    Assert-Condition -Condition ($extent.Success) -Message ($Label + ": no Extent line in info output: " + $Text)
    $fieldCount = [regex]::Match($Text, "(?m)^\s*Fields:\s+(\d+)\s*$")
    Assert-Condition -Condition ($fieldCount.Success) -Message ($Label + ": no Fields line in info output: " + $Text)
    $fieldMatches = [regex]::Matches($Text, "(?m)^\s*\[(\d+)\]\s+(.+?)\s+\(([^)]+)\)\s*$")
    $fieldNames = @()
    $fieldTypes = @()
    foreach ($m in $fieldMatches) {
        $fieldNames += $m.Groups[2].Value
        $fieldTypes += $m.Groups[3].Value
    }
    Assert-Condition -Condition ($fieldNames.Count -eq [int]$fieldCount.Groups[1].Value) -Message (
        $Label + ": info says Fields: " + $fieldCount.Groups[1].Value + " but parsed " + $fieldNames.Count + " field line(s)")

    return [pscustomobject]@{
        Driver = $driver.Groups[1].Value
        LayerCount = [int]$layers.Groups[1].Value
        LayerName = $layerName.Groups[1].Value
        Geometry = $geometry.Groups[1].Value
        Crs = $crs.Groups[1].Value
        FeatureCount = [int]$features.Groups[1].Value
        FieldCount = [int]$fieldCount.Groups[1].Value
        FieldNames = $fieldNames
        FieldTypes = $fieldTypes
        MinX = [double]$extent.Groups[1].Value
        MinY = [double]$extent.Groups[2].Value
        MaxX = [double]$extent.Groups[3].Value
        MaxY = [double]$extent.Groups[4].Value
    }
}

# 对**文件**跑一次 vector info 再解析。D1 未销账：info 仍被插件级 output 必填挡着，
# 所以带一个不会落盘的占位输出（doInfo 不写文件）。
function Get-LayerInfoForPath {
    param(
        [string]$ResolvedCliPath,
        [string]$Path,
        [string]$ScratchDir,
        [string]$Label
    )

    $text = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
        "vector", "info",
        ("--input=" + $Path),
        ("--output=" + (Join-Path $ScratchDir ("info_" + $Label + "_placeholder.gpkg")))
    )
    return Get-LayerInfoSummary -Text $text -Label $Label
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

# N143 之后新立的取数口：矢量动作的要素数一律走自报的 metadata 键，不再抠 "X completed: N features"
# 这类散文消息（抠散文时消息文案一改判据就红，而"报出来的数"本身反而没有整行锚定）。
function Assert-ReportedMetadataCountEquals {
    param(
        [string]$Text,
        [string]$Key,
        [int]$Expected,
        [string]$Label
    )

    $actual = Get-ReportedMetadataCount -Text $Text -Key $Key -Label $Label
    Assert-Condition -Condition ($actual -eq $Expected) -Message (
        $Label + ": reported " + $Key + " " + $actual + ", expected " + $Expected)
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

    # C3k 拆腿之后 C3a 这批派生件只由 overlay_buffer_join / overlay_ops 两条腿构造。这条腿若被归到别的档却真的跑了起来，
    # 传进来的现读属性就是 $null —— 必须在读产物之前判红：PS 里 $null.X 取到 $null，
    # 后面的比较判据会退化成"既不满足也不报错"，等于把归错档洗成假绿。
    # （同形判据已存在于 Assert-TopologyCheckCase 与 Assert-ChineseAttributeFilterCase。）
    Assert-Condition -Condition ($null -ne $InputStats) -Message (
        $Name + ": C3a derived input stats are null, so this leg ran in a group that never built its inputs")

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
        [string]$RoadsCore3857,
        # C3k 拆腿之后本函数只被 overlay_buffer_join / overlay_ops 两条腿真正需要。-PathsOnly 把"算路径"与"造派生件"分开：
        # 路径表达式仍然只有这一处（注册用例要用路径，但 checks 那一半不造件），
        # 调用方在没造件时把 refs 置 $null => "腿被归错档却真跑起来"会红在
        # Assert-DerivedGeometryCase 的"现读输入属性"判据上，而不是拿空对象蒙过去。
        [switch]$PathsOnly
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
    $lineInput = Join-Path $scratchRoot "roads_3857.gpkg"

    # 三个 extract 跑在"真实线的切片"上：整层 22836 要素在 Debug 树实测
    # vertices 563.9 s + endpoints 115.9 s + midpoints 105.5 s = 785.3 s，
    # 单这三条就顶穿本腿 720 s 的 TIMEOUT（本腿今天整条 Debug 热跑 170.935 s）⇒ 取真实图层的
    # 前 800 个要素做切片（实测切片本身 0.9 s、三条合计 15.6 s）。切片仍由真实几何构成，
    # 判据也全部从切片自己的现读属性推导，不写死数字。
    $lineSlice = Join-Path $scratchRoot "roads_3857_slice.gpkg"

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

    if ($PathsOnly) {
        return [pscustomobject]@{
            ScratchRoot = $scratchRoot
            PolygonInput = $polygonInput
            LineInput = $lineInput
            LineSlice = $lineSlice
            Outputs = $outputs
            PolygonStats = $null
            LineStats = $null
        }
    }

    Copy-Item $provinceRaw $polygonInput -Force
    Copy-Item $RoadsCore3857 $lineInput -Force
    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_derived_line_slice" -CaseArgs @(
        "vector", "filter",
        ("--input=" + $lineInput),
        ("--output=" + $lineSlice),
        "--where=fid <= 800"
    )

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

# ── C3b-A：vector 拓扑/检查批 11 动作的真实数据腿（只在 Mode=full 注册，归 Group=checks，
#    即 ctest 用例 real_data_vector_checks）──
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
        [string[]]$ExtraArgs,
        # C3b-A 这批腿整体消费 w212/buf30/prov3857/province_boundary/ring_endpoints 这一套派生件
        # => 缺省归 checks 那一半；-Group 允许显式改写，只为让"故意破坏"那一次跑能真的把档归错。
        [string]$Group = "checks"
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

    return New-Case -Name $Name -CaseArgs $caseArgs -ExpectedOutputs @($OutputPath) -Group $Group
}

function Ensure-TopologyCheckInputs {
    param(
        [string]$ResolvedCliPath,
        [string]$ResolvedWorkspaceRoot,
        [string]$RoadsCore3857,
        # C3k 拆腿之后 w212/buf30/prov3857/province_boundary/ring_endpoints 这一套派生件
        # 只有 checks 那一半消费。-PathsOnly 只把路径算出来（路径表达式仍然只有这一处）就返回，
        # overlay 两条腿于是既不造件也不删目录；调用方在没造件时把 refs 置 $null，
        # "某条 checks 腿被归错档却真跑起来"就会红在下面那句 TopologyRefs null 的判据上。
        [switch]$PathsOnly
    )

    $scratchRoot = Join-Path $ResolvedWorkspaceRoot "tmp\vector_benchmark\c3ba"
    $lineWindow = Join-Path $scratchRoot "w212.gpkg"
    $bufferPolygons = Join-Path $scratchRoot "buf30.gpkg"
    $provinceProjected = Join-Path $scratchRoot "prov3857.gpkg"
    $provinceBoundary = Join-Path $scratchRoot "province_boundary.gpkg"
    $ringEndpoints = Join-Path $scratchRoot "ring_endpoints.gpkg"

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

    if ($PathsOnly) {
        return [pscustomobject]@{
            ScratchRoot = $scratchRoot
            LineWindow = $lineWindow
            BufferPolygons = $bufferPolygons
            ProvinceProjected = $provinceProjected
            ProvinceBoundary = $provinceBoundary
            RingEndpoints = $ringEndpoints
            WindowStats = $null
            BufferStats = $null
            ProvinceStats = $null
            BoundaryStats = $null
            EndpointStats = $null
            Outputs = $outputs
        }
    }

    # 派生件一律现造、不缓存：控制器 2026-10-22 的探针在 build/tmp/c3ba 留过同名文件，
    # "上次造过所以这次跳过"会让这条腿在清过 build 的机器上行为不同，也会让耗时低估
    # —— 而工单要求加完腿之后**现跑现报**该条的墙钟。
    if (Test-Path $scratchRoot) {
        Remove-Item -Recurse -Force $scratchRoot
    }
    New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

    # data/ 只读（N115）：喂 CLI 的一律是复制件，边车（.aux.xml）只可能种在本腿的临时区。
    $vectorDataRoot = Join-Path $ResolvedWorkspaceRoot "data\vector"
    $provinceRaw = Resolve-ProvinceVectorInput -VectorDataRoot $vectorDataRoot -Label "topology check leg"
    $provinceGeojson = Join-Path $scratchRoot "province_raw.geojson"
    Copy-Item $provinceRaw $provinceGeojson -Force

    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_line_window" -CaseArgs @(
        "vector", "filter",
        ("--input=" + $RoadsCore3857),
        ("--output=" + $lineWindow),
        "--extent=12950000,4890000,12970000,4910000"
    )

    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_buffer_polygons" -CaseArgs @(
        "vector", "buffer",
        ("--input=" + $lineWindow),
        ("--output=" + $bufferPolygons),
        "--distance=30"
    )

    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_projected_provinces" -CaseArgs @(
        "projection", "reproject",
        ("--input=" + $provinceGeojson),
        ("--output=" + $provinceProjected),
        "--dst_srs=EPSG:3857"
    )

    Invoke-SetupCommand -ResolvedCliPath $ResolvedCliPath -Name "prepare_c3ba_province_boundary" -CaseArgs @(
        "vector", "boundary",
        ("--input=" + $provinceProjected),
        ("--output=" + $provinceBoundary)
    )

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
            # 两动作各自实现"配对枚举 + 写行 + Equals/else-if 互斥"这一层 ⇒ 等式交叉验证的是这层；
            # ⚠ N125 修复后**交面积已单源**（两边都调 polygonIntersectionArea），
            #   所以这条等式**不再**独立验证面积算法本身——别把它读成"两套面积实现的互证"。
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
            $selfCount = Get-ReportedMetadataCount -Text $selfText `
                -Key "feature_count" -Label ($Case.Name + " nearest_self filter feature_count")
            Assert-Condition -Condition ($selfCount -eq 0) -Message (
                $Case.Name + ":" + $selfCount + " nearest_fid == fid sameDataset FID")
        }
        default {
            throw ($Case.Name + " C3b-A Assert-TopologyCheckCase")
        }
    }
}

# ── C3f / N127：--where 用中文属性值过滤真实省层（真实数据腿）──
# 编码纪律（先读这段再动这一批函数）：本文件是无 BOM 的 UTF-8 + CRLF，PowerShell 5.1 用 -File
#   按 ANSI 代码页解码源文件 ⇒ **中文字面量只能出现在注释里**，出现在字符串里就是乱码路径。
#   要用的中文属性值一律在运行时从输入自己的 JSON 现读（Get-Content -Encoding UTF8），
#   这也顺带满足工单"期望值从输入侧手推，不从产物回填"。
# 现读到的输入层形状（vector info + 同一份复制件的 JSON 对撞）：
#   data/vector/中国_省.geojson 的复制件 = 42 要素（34 面 + 8 线，Geometry: Unknown），
#   CRS EPSG:4490，字段两个 name(String，全中文) 与 gb(String，纯数字)，name 共 35 个不同取值、
#   众数是"境界线"那一档（8 条）。
# 判据全部是关系式，期望值由输入自己推出 ⇒ 换一份数据文件照样成立，写死数字就做不到：
#   ① 目标值必须含非 ASCII 码点 ⇒ 这条腿测的确实是 N127 那半边（选中纯 ASCII 值时旧实现也过，腿就白搭）；
#   ② 目标值必须不含单引号 ⇒ WHERE 用单引号包裹，含引号时构造本身不成立，先红在这里而不是红在 GDAL；
#   ③ 1 <= M < 全层要素数（M = 输入侧数出来的 name==目标值 的条数）⇒ 过滤真的丢了东西，
#      选众数而不是选第一条就是为了让 M > 1，于是"匹配到第一条就 break"这种实现必然红；
#   ④ CLI 自报数 == vector info 现读的输出 Features == M 三段等式；
#      ⚠ filter **没有** feature_count 元数据（doFilter 只回消息与输出路径，见
#        src/plugins/vector/vector_plugin_info_filter.inc 的 return 与
#        docs/算法说明/矢量数据处理/空间过滤.md"返回消息中的数量"）⇒ 自报数只能取消息里那个数；
#   ⑤ 输出 Extent == 输入侧那 M 条要素自己坐标的包框（从 JSON 现算）；
#   ⑥ 输出 Extent 的四边不得与输入全层 Extent 逐边相同 ⇒ 打死"WHERE 被当成没写"（恒等产物）；
#   ⑦ 输出 Extent 必须仍被输入全层 Extent 包含 ⇒ 没被空间过滤误伤、也没凭空放大；
#   ⑧ 输出字段数 == 输入字段数（filter 不删字段）。
# ⑤ 的量纲不是 1e-6：vector info 用默认 6 位有效数字打印坐标，122.816588 会被打成 "122.817"
#   （偏差上限 ~5e-4，是本仓既有 1e-6 绝对容差撑不住的量级），所以这里按**相对**容差判，
#   取 1e-5（本层量级 ~1e2 ⇒ 绝对上限 ~1.3e-3 度）：比 %g 舍入宽，比最窄的一条被留要素（0.106 度）窄。
$script:C3fExtentRelativeTolerance = 1e-5

function Add-JsonNumberLeaves {
    param($Node, $Acc, [ref]$Skipped)

    # GeoJSON 的 coordinates 是任意深度的嵌套数组，最深一层是 [x, y]（本层无 Z）。
    # 不认识的东西不静默丢：计入 Skipped，调用方判它必须为 0 ⇒ "解析器归零"这类假绿没有藏身处。
    if ($Node -is [array]) {
        foreach ($child in $Node) {
            Add-JsonNumberLeaves -Node $child -Acc $Acc -Skipped $Skipped
        }
    } elseif ($Node -is [double] -or $Node -is [int] -or $Node -is [long] -or $Node -is [decimal]) {
        $Acc.Add([double]$Node) | Out-Null
    } else {
        $Skipped.Value = $Skipped.Value + 1
    }
}

function Get-FeaturesExtentStats {
    param([array]$Features, [string]$Label)

    # 输入侧手推包框：逐条要素把坐标摊平成 x,y 序列，再取四极值。
    # 要素为空、坐标个数不是偶数、个数 < 4 都当场红 —— 期望值一旦来自"读不到就算 0"，
    # 后面 ⑤⑥⑦ 三条判据会集体失去判别力。
    $xs = New-Object "System.Collections.Generic.List[double]"
    $ys = New-Object "System.Collections.Generic.List[double]"
    $skipped = 0
    foreach ($feature in $Features) {
        $leaves = New-Object "System.Collections.Generic.List[double]"
        Add-JsonNumberLeaves -Node $feature.geometry.coordinates -Acc $leaves -Skipped ([ref]$skipped)
        Assert-Condition -Condition ($leaves.Count -ge 4 -and ($leaves.Count % 2) -eq 0) -Message (
            $Label + ": a kept feature flattens to " + $leaves.Count +
            " coordinate numbers, expected an even count >= 4 (x,y pairs; 3D/Z coordinates are out of scope here)")
        for ($i = 0; $i -lt $leaves.Count; $i += 2) {
            $xs.Add($leaves[$i]) | Out-Null
            $ys.Add($leaves[$i + 1]) | Out-Null
        }
    }
    Assert-Condition -Condition ($skipped -eq 0) -Message (
        $Label + ": coordinate tree left " + $skipped + " unparsed node(s) => the extent expectation would be built on partial data")
    Assert-Condition -Condition ($xs.Count -ge 2) -Message ($Label + ": no coordinate pair collected")

    return [pscustomobject]@{
        MinX = ($xs | Measure-Object -Minimum).Minimum
        MinY = ($ys | Measure-Object -Minimum).Minimum
        MaxX = ($xs | Measure-Object -Maximum).Maximum
        MaxY = ($ys | Measure-Object -Maximum).Maximum
    }
}

function Ensure-ChineseAttributeFilterInput {
    param(
        [string]$ResolvedCliPath,
        [string]$ResolvedWorkspaceRoot
    )

    # N115：data/ 只许被复制，边车（.aux.xml 一类）必须种在 build/tmp/ 里。
    $scratchRoot = Join-Path $ResolvedWorkspaceRoot "build\tmp\c3f"
    New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

    $vectorDataRoot = Join-Path $ResolvedWorkspaceRoot "data\vector"
    $provinceRaw = Resolve-ProvinceVectorInput -VectorDataRoot $vectorDataRoot -Label "chinese attribute filter leg"
    $inputPath = Join-Path $scratchRoot "province.geojson"
    Copy-Item $provinceRaw $inputPath -Force

    $layerStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $inputPath -ScratchDir $scratchRoot
    Assert-Condition -Condition ($layerStats.FeatureCount -gt 1) -Message (
        "chinese attribute filter leg: input layer has " + $layerStats.FeatureCount +
        " feature(s); a single-feature layer cannot show whether the WHERE clause filtered anything")

    # PS 5.1 的 Get-Content 不带 -Encoding 按 ANSI 代码页解码，UTF-8 的中文属性值会被读坏，
    # 报出来的是"JSON 无效/匹配不到"这种假产品缺陷 ⇒ -Encoding UTF8 是这条腿的前提，不是风格问题。
    $jsonText = Get-Content -Path $inputPath -Raw -Encoding UTF8
    Assert-Condition -Condition ($null -ne $jsonText -and $jsonText.Length -gt 0) -Message (
        "chinese attribute filter leg: input copy read back empty: " + $inputPath)
    $doc = $jsonText | ConvertFrom-Json
    Assert-Condition -Condition ($null -ne $doc.features) -Message (
        "chinese attribute filter leg: GeoJSON has no features member: " + $inputPath)
    $featureCount = @($doc.features).Count
    Assert-Condition -Condition ($featureCount -eq $layerStats.FeatureCount) -Message (
        "chinese attribute filter leg: JSON carries " + $featureCount + " feature(s) but vector info reports " +
        $layerStats.FeatureCount + " (input side disagrees with itself, expectation cannot be trusted)")

    $fieldNames = @()
    foreach ($feature in @($doc.features)) {
        foreach ($prop in $feature.properties.PSObject.Properties) {
            if ($fieldNames -notcontains $prop.Name) { $fieldNames += $prop.Name }
        }
    }
    Assert-Condition -Condition ($fieldNames.Count -ge 1) -Message (
        "chinese attribute filter leg: no property found in " + $inputPath)
    if ($null -ne $layerStats.FieldCount) {
        Assert-Condition -Condition ($fieldNames.Count -eq $layerStats.FieldCount) -Message (
            "chinese attribute filter leg: JSON exposes " + $fieldNames.Count + " property name(s) but vector info reports Fields: " +
            $layerStats.FieldCount + " (field list disagrees between the two readers)")
    }

    # 选字段：第一个"取值里出现非 ASCII 码点"的字符串字段。本层实测选中 name（全中文），
    # gb 是纯数字 ⇒ 选不中。写成"按码点找"而不是"写死 name"，换一份数据也不会悄悄测到 ASCII 那半边。
    $chosenField = ""
    $chosenValues = @()
    foreach ($candidate in $fieldNames) {
        $values = @(@($doc.features) | ForEach-Object { $_.properties.$candidate } | Where-Object { $_ -is [string] -and $_ -ne "" })
        $nonAscii = @($values | Where-Object { [regex]::IsMatch($_, "[^\x00-\x7F]") })
        if ($nonAscii.Count -gt 0) { $chosenField = $candidate; $chosenValues = $values; break }
    }
    Assert-Condition -Condition ($chosenField -ne "") -Message (
        "chinese attribute filter leg: no string field carries a non-ASCII value, so this leg cannot exercise N127 " +
        "(fields found: " + ($fieldNames -join ", ") + ")")

    # 目标值取该字段出现次数最多的那一档（众数）：M > 1 才能打死"匹配到第一条就 break"。
    $groups = @($chosenValues | Group-Object)
    $distinctValues = $groups.Count
    $modeGroup = @($groups | Sort-Object -Property Count -Descending | Select-Object -First 1)[0]
    $targetValue = [string]$modeGroup.Name
    $matchCount = [int]$modeGroup.Count
    $keptFeatures = @(@($doc.features) | Where-Object { [string]$_.properties.$chosenField -eq $targetValue })

    Assert-Condition -Condition ($keptFeatures.Count -eq $matchCount) -Message (
        "chinese attribute filter leg: group-by says " + $matchCount + " but the scan found " + $keptFeatures.Count)
    Assert-Condition -Condition ([regex]::IsMatch($targetValue, "[^\x00-\x7F]")) -Message (
        "chinese attribute filter leg: chosen value is pure ASCII, this leg would pass on the pre-N127 whitelist")
    Assert-Condition -Condition (-not $targetValue.Contains("'")) -Message (
        "chinese attribute filter leg: chosen value contains a single quote, the WHERE construction below would not be valid")
    Assert-Condition -Condition ($matchCount -ge 1 -and $matchCount -lt $featureCount -and $distinctValues -gt 1) -Message (
        "chinese attribute filter leg: matched " + $matchCount + " of " + $featureCount +
        " feature(s) over " + $distinctValues + " distinct value(s); the leg needs a value that keeps something AND drops something")

    $where = $chosenField + "='" + $targetValue + "'"
    $outputPath = Join-Path $scratchRoot "filter_cn_attribute.gpkg"
    $keptExtent = Get-FeaturesExtentStats -Features $keptFeatures -Label "chinese attribute filter leg"
    $largestAbs = 0.0
    foreach ($v in @($keptExtent.MinX, $keptExtent.MaxX, $keptExtent.MinY, $keptExtent.MaxY,
                      $layerStats.MinX, $layerStats.MaxX, $layerStats.MinY, $layerStats.MaxY)) {
        if ([Math]::Abs($v) -gt $largestAbs) { $largestAbs = [Math]::Abs($v) }
    }
    $extentTolerance = [Math]::Max($largestAbs * $script:C3fExtentRelativeTolerance, 1e-9)

    Write-Host ("  c3f leg: field=" + $chosenField + " codepoints=" +
        ((@($targetValue.ToCharArray() | ForEach-Object { "U+{0:X4}" -f [int]$_ })) -join " ") +
        " matched=" + $matchCount + "/" + $featureCount)

    return [pscustomobject]@{
        ScratchRoot = $scratchRoot
        InputPath = $inputPath
        OutputPath = $outputPath
        FieldName = $chosenField
        TargetValue = $targetValue
        MatchCount = $matchCount
        LayerStats = $layerStats
        KeptExtent = $keptExtent
        ExtentTolerance = $extentTolerance
        Where = $where
    }
}

function Assert-ChineseAttributeFilterCase {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [object]$Refs
    )

    Assert-Condition -Condition ($null -ne $Refs) -Message (
        $Case.Name + ": the chinese attribute filter leg is registered but its input refs are missing " +
        "(Ensure-ChineseAttributeFilterInput did not run) => every expectation would be vacuous")
    Assert-Condition -Condition ($Case.Name -eq "filter_cn_attribute") -Message (
        "Assert-ChineseAttributeFilterCase got '" + $Case.Name + "'")

    $inputStats = $Refs.LayerStats
    $tolerance = [double]$Refs.ExtentTolerance
    $read = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $Refs.OutputPath -ScratchDir $Refs.ScratchRoot

    Assert-Condition -Condition ($read.FeatureCount -ge 1) -Message (
        $Case.Name + ": output layer keeps " + $read.FeatureCount + " feature(s), expected at least 1")
    # ④ 三段等式：CLI 自报数 / 落盘现读数 / 输入侧手推数。
    $reported = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" `
        -Label ($Case.Name + " reported feature_count")
    Assert-Condition -Condition ($reported -eq $read.FeatureCount) -Message (
        $Case.Name + ": CLI says " + $reported + " but vector info reads back " + $read.FeatureCount +
        " (自报数与落盘数不一致)")
    Assert-Condition -Condition ($read.FeatureCount -eq $Refs.MatchCount) -Message (
        $Case.Name + ": output keeps " + $read.FeatureCount + " feature(s), input side counted " +
        $Refs.MatchCount + " for the same value (filter dropped or invented features)")
    # ⑧ 字段结构原样保留
    if ($null -ne $inputStats.FieldCount -and $null -ne $read.FieldCount) {
        Assert-Condition -Condition ($read.FieldCount -eq $inputStats.FieldCount) -Message (
            $Case.Name + ": output has " + $read.FieldCount + " field(s), input has " + $inputStats.FieldCount)
    }
    # ⑤ 输出 Extent == 被留要素在输入里的包框
    Assert-ExtentEqualTo -Expected $Refs.KeptExtent -Actual $read -Tolerance $tolerance -Label $Case.Name
    # ⑦ 仍被输入全层范围包含
    Assert-ExtentInsideOf -Outer $inputStats -Inner $read -Tolerance $tolerance -Label $Case.Name
    # ⑥ 四边不得与输入全层逐边相同（"WHERE 被当成没写"的产物恰好满足 ⑤⑦ 之外的全部判据吗？不满足：
    #    它会把 Extent 打回全层范围 ⇒ 这一条专抓它；M < 全层时 ④ 也抓它，两条互为备份）
    $sameEdges = @(
        ([Math]::Abs($read.MinX - $inputStats.MinX) -le $tolerance),
        ([Math]::Abs($read.MinY - $inputStats.MinY) -le $tolerance),
        ([Math]::Abs($read.MaxX - $inputStats.MaxX) -le $tolerance),
        ([Math]::Abs($read.MaxY - $inputStats.MaxY) -le $tolerance)
    )
    Assert-Condition -Condition (@($sameEdges | Where-Object { $_ -eq $false }).Count -ge 1) -Message (
        $Case.Name + ": output extent equals the whole input extent => the WHERE clause behaved like a no-op")

    return $read
}

# ── C3c-A：info / repair 的真实数据 quick 腿；convert 已有 convert_roads_geojson，只增强判据 ──
# 两份输入都先复制到 build/tmp/c3cA/ 再喂 CLI（N115：info 会在 GPKG 输入旁写 .aux.xml，
# 本刀 §0.2 已现读复现）。province 的期望值全部从 GeoJSON 自己现读（要素数/字段顺序/
# 几何类型种类/CRS/extent），roads 的 22836 要素与 9 个字段是该基准件的**数据契约**
# （bj_roads_core 由 data/vector 最小 shapefile 派生，见 Ensure-BenchmarkData）。
function Ensure-C3cARealInputs {
    param(
        [string]$ResolvedWorkspaceRoot
    )

    $scratchRoot = Join-Path $ResolvedWorkspaceRoot "build\tmp\c3cA"
    New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

    $vectorDataRoot = Join-Path $ResolvedWorkspaceRoot "data\vector"
    $provinceRaw = Resolve-ProvinceVectorInput -VectorDataRoot $vectorDataRoot -Label "C3c-A info/repair legs"
    $provinceCopy = Join-Path $scratchRoot "china_provinces.geojson"
    Copy-Item $provinceRaw $provinceCopy -Force

    $roadsBenchmark = Join-Path $ResolvedWorkspaceRoot "tmp\vector_benchmark\bj_roads_core_3857.gpkg"
    Require-Path -Path $roadsBenchmark -Label "C3c-A roads benchmark"
    $roadsCopy = Join-Path $scratchRoot "bj_roads_core_3857.gpkg"
    Copy-Item $roadsBenchmark $roadsCopy -Force
    # 上一轮留下的边车不许替本轮"证明 info 会写 .aux.xml"——先删，本轮跑完再由 info_roads 的判据要求它在场。
    Remove-Item ($roadsCopy + ".aux.xml") -Force -ErrorAction SilentlyContinue

    # PS 5.1 的 Get-Content 不带 -Encoding 按 ANSI 解码，UTF-8 的中文属性值会被读坏 ⇒ 必须 UTF8。
    $jsonText = Get-Content -Path $provinceCopy -Raw -Encoding UTF8
    Assert-Condition -Condition ($null -ne $jsonText -and $jsonText.Length -gt 0) -Message (
        "C3c-A: province copy read back empty: " + $provinceCopy)
    $doc = $jsonText | ConvertFrom-Json
    $features = @($doc.features)
    Assert-Condition -Condition ($features.Count -ge 2) -Message (
        "C3c-A: province copy has " + $features.Count + " feature(s), expected a real layer")
    $provinceExtent = Get-FeaturesExtentStats -Features $features -Label "C3c-A province copy"

    $fieldNames = @()
    foreach ($feature in $features) {
        $current = @($feature.properties.PSObject.Properties | ForEach-Object { $_.Name })
        if ($fieldNames.Count -eq 0) { $fieldNames = $current }
        Assert-Condition -Condition (($current -join ",") -ceq ($fieldNames -join ",")) -Message (
            "C3c-A: a province feature's property list differs from the first feature's (" +
            ($current -join ",") + " vs " + ($fieldNames -join ",") + "); info cannot derive a single field-list expectation from this input")
    }
    Assert-Condition -Condition ($fieldNames.Count -ge 1) -Message ("C3c-A: province copy carries no property")

    $geomTypes = @($features | ForEach-Object { [string]$_.geometry.type } | Sort-Object -Unique)
    $provinceGeometryKind = if ($geomTypes.Count -eq 1) { [string]$geomTypes[0] } else { "Unknown" }

    $crsName = ""
    if ($null -ne $doc.crs -and $null -ne $doc.crs.properties) { $crsName = [string]$doc.crs.properties.name }
    $provinceCrs = ""
    if ($crsName -match 'EPSG:{1,2}(\d+)') { $provinceCrs = "EPSG:" + $Matches[1] }
    Assert-Condition -Condition ($provinceCrs -ne "") -Message (
        "C3c-A: cannot derive an EPSG code from the GeoJSON crs name '" + $crsName + "'")

    $largestAbs = 0.0
    foreach ($v in @($provinceExtent.MinX, $provinceExtent.MaxX, $provinceExtent.MinY, $provinceExtent.MaxY)) {
        if ([Math]::Abs([double]$v) -gt $largestAbs) { $largestAbs = [Math]::Abs([double]$v) }
    }
    $extentTolerance = [Math]::Max($largestAbs * $script:C3fExtentRelativeTolerance, 1e-9)

    Write-Host ("  c3c-A inputs: province=" + $features.Count + " feature(s), geometry=" + ($geomTypes -join "+") +
        ", crs=" + $provinceCrs + "; roads copy=" + $roadsCopy)

    return [pscustomobject]@{
        ScratchRoot = $scratchRoot
        ProvinceCopy = $provinceCopy
        ProvinceLayerName = [System.IO.Path]::GetFileNameWithoutExtension($provinceCopy)
        ProvinceFeatureCount = $features.Count
        ProvinceFieldNames = $fieldNames
        ProvinceGeometryKind = $provinceGeometryKind
        ProvinceCrs = $provinceCrs
        ProvinceExtent = $provinceExtent
        ProvinceExtentTolerance = $extentTolerance
        RoadsCopy = $roadsCopy
    }
}

function Assert-C3cAInfoProvinceCase {
    param(
        [pscustomobject]$Case,
        [object]$Refs
    )

    Assert-Condition -Condition ($null -ne $Refs) -Message (
        $Case.Name + ": C3c-A refs missing (Ensure-C3cARealInputs did not run) => expectations would be vacuous")
    Assert-Condition -Condition ($Case.Name -eq "info_province") -Message (
        "Assert-C3cAInfoProvinceCase got '" + $Case.Name + "'")

    $info = Get-LayerInfoSummary -Text $Case.Output -Label $Case.Name
    Assert-Condition -Condition ($info.Driver -eq "GeoJSON") -Message (
        $Case.Name + ": Driver=" + $info.Driver + ", expected GeoJSON")
    Assert-Condition -Condition ($info.LayerCount -eq 1) -Message (
        $Case.Name + ": Layers=" + $info.LayerCount + ", expected 1")
    Assert-Condition -Condition ($info.LayerName -ceq $Refs.ProvinceLayerName) -Message (
        $Case.Name + ": LayerName='" + $info.LayerName + "', expected '" + $Refs.ProvinceLayerName + "'")
    Assert-Condition -Condition ($info.Geometry -ceq $Refs.ProvinceGeometryKind) -Message (
        $Case.Name + ": Geometry='" + $info.Geometry + "', input side derives '" + $Refs.ProvinceGeometryKind + "'")
    Assert-Condition -Condition ($info.Crs -ceq $Refs.ProvinceCrs) -Message (
        $Case.Name + ": CRS='" + $info.Crs + "', input GeoJSON says '" + $Refs.ProvinceCrs + "'")
    Assert-Condition -Condition ($info.FeatureCount -eq $Refs.ProvinceFeatureCount) -Message (
        $Case.Name + ": Features=" + $info.FeatureCount + ", input JSON has " + $Refs.ProvinceFeatureCount)
    Assert-Condition -Condition ($info.FieldCount -eq @($Refs.ProvinceFieldNames).Count) -Message (
        $Case.Name + ": Fields=" + $info.FieldCount + ", input JSON has " + @($Refs.ProvinceFieldNames).Count + " property name(s)")
    Assert-Condition -Condition (($info.FieldNames -join ",") -ceq (@($Refs.ProvinceFieldNames) -join ",")) -Message (
        $Case.Name + ": field list/order '" + ($info.FieldNames -join ",") + "' != input JSON property order '" +
        (@($Refs.ProvinceFieldNames) -join ",") + "'")
    Assert-ExtentEqualTo -Expected $Refs.ProvinceExtent -Actual $info `
        -Tolerance $Refs.ProvinceExtentTolerance -Label $Case.Name
    $script:C3cAAssertedCount = $script:C3cAAssertedCount + 1
    return $info
}

function Assert-C3cAInfoRoadsCase {
    param(
        [pscustomobject]$Case,
        [object]$Refs,
        [int]$ExpectedFeatureCount
    )

    Assert-Condition -Condition ($null -ne $Refs) -Message (
        $Case.Name + ": C3c-A refs missing (Ensure-C3cARealInputs did not run)")
    Assert-Condition -Condition ($Case.Name -eq "info_roads") -Message (
        "Assert-C3cAInfoRoadsCase got '" + $Case.Name + "'")

    $info = Get-LayerInfoSummary -Text $Case.Output -Label $Case.Name
    Assert-Condition -Condition ($info.Driver -eq "GPKG") -Message ($Case.Name + ": Driver=" + $info.Driver + ", expected GPKG")
    Assert-Condition -Condition ($info.LayerCount -eq 1) -Message ($Case.Name + ": Layers=" + $info.LayerCount + ", expected 1")
    Assert-Condition -Condition ($info.LayerName -ceq $script:RoadsCoreLayerName) -Message (
        $Case.Name + ": LayerName='" + $info.LayerName + "', expected '" + $script:RoadsCoreLayerName + "'")
    Assert-Condition -Condition ($info.Geometry -ceq "MultiLineString") -Message ($Case.Name + ": Geometry='" + $info.Geometry + "', expected MultiLineString")
    Assert-Condition -Condition ($info.Crs -ceq "EPSG:3857") -Message ($Case.Name + ": CRS='" + $info.Crs + "', expected EPSG:3857")
    Assert-Condition -Condition ($info.FeatureCount -eq $ExpectedFeatureCount) -Message (
        $Case.Name + ": Features=" + $info.FeatureCount + ", roads core reports " + $ExpectedFeatureCount)
    $expectedFields = $script:RoadsCoreFieldNames
    Assert-Condition -Condition ($info.FieldCount -eq $expectedFields.Count) -Message (
        $Case.Name + ": Fields=" + $info.FieldCount + ", expected " + $expectedFields.Count)
    Assert-Condition -Condition (($info.FieldNames -join ",") -ceq ($expectedFields -join ",")) -Message (
        $Case.Name + ": field list/order '" + ($info.FieldNames -join ",") + "' != '" + ($expectedFields -join ",") + "'")
    # N115 实证：info 在 GPKG 复制件旁写边车 —— 边车只许落在 build/tmp/c3cA/，data/ 侧由 find 证明为 0。
    Assert-Condition -Condition (Test-Path ($Refs.RoadsCopy + ".aux.xml")) -Message (
        $Case.Name + ": expected the .aux.xml sidecar next to the copied GPKG (" + $Refs.RoadsCopy + ".aux.xml)")
    $script:C3cAAssertedCount = $script:C3cAAssertedCount + 1
    return $info
}

function Assert-C3cARepairCase {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [string]$ResolvedOutputRoot,
        [object]$Refs
    )

    Assert-Condition -Condition ($null -ne $Refs) -Message (
        $Case.Name + ": C3c-A refs missing (Ensure-C3cARealInputs did not run)")
    Assert-Condition -Condition ($Case.Name -eq "repair_province") -Message (
        "Assert-C3cARepairCase got '" + $Case.Name + "'")

    $reported = Get-ReportedCount -Text $Case.Output -Pattern "Repair completed: (\d+) features" -Label ($Case.Name + " reported count")
    Assert-Condition -Condition ($reported -eq $Refs.ProvinceFeatureCount) -Message (
        $Case.Name + ": CLI reports " + $reported + " repaired, input JSON has " + $Refs.ProvinceFeatureCount)
    $repaired = Get-ReportedMetadataCount -Text $Case.Output -Key "repaired_count" -Label ($Case.Name + " repaired_count")
    Assert-Condition -Condition ($repaired -eq $Refs.ProvinceFeatureCount) -Message (
        $Case.Name + ": repaired_count=" + $repaired + ", expected " + $Refs.ProvinceFeatureCount)
    $skipped = Get-ReportedMetadataCount -Text $Case.Output -Key "skipped_count" -Label ($Case.Name + " skipped_count")
    Assert-Condition -Condition ($skipped -eq 0) -Message (
        $Case.Name + ": skipped_count=" + $skipped + "; every province geometry is valid, repair must not drop any")

    $outputPath = Join-Path $ResolvedOutputRoot "repair_province.gpkg"
    $read = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $outputPath `
        -ScratchDir $Refs.ScratchRoot -Label "repair_province_output"
    Assert-Condition -Condition ($read.FeatureCount -eq $Refs.ProvinceFeatureCount) -Message (
        $Case.Name + ": output has " + $read.FeatureCount + " feature(s), input has " + $Refs.ProvinceFeatureCount)
    Assert-Condition -Condition (($read.FieldNames -join ",") -ceq (@($Refs.ProvinceFieldNames) -join ",")) -Message (
        $Case.Name + ": output fields '" + ($read.FieldNames -join ",") + "' != input '" + (@($Refs.ProvinceFieldNames) -join ",") + "'")
    Assert-Condition -Condition ($read.LayerName -ceq $Refs.ProvinceLayerName) -Message (
        $Case.Name + ": output LayerName='" + $read.LayerName + "', expected '" + $Refs.ProvinceLayerName + "'")
    Assert-Condition -Condition ($read.Geometry -ceq $Refs.ProvinceGeometryKind) -Message (
        $Case.Name + ": output Geometry='" + $read.Geometry + "', input is '" + $Refs.ProvinceGeometryKind + "'")
    Assert-Condition -Condition ($read.Crs -ceq $Refs.ProvinceCrs) -Message (
        $Case.Name + ": output CRS='" + $read.Crs + "', input is '" + $Refs.ProvinceCrs + "'")
    # repair 对合法几何是恒等（SRS/坐标原样克隆）⇒ 输出包框必须等于输入 JSON 手推包框。
    Assert-ExtentEqualTo -Expected $Refs.ProvinceExtent -Actual $read `
        -Tolerance $Refs.ProvinceExtentTolerance -Label $Case.Name
    $script:C3cAAssertedCount = $script:C3cAAssertedCount + 1
    return $read
}

function Assert-C3cAConvertCase {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [string]$ResolvedOutputRoot,
        [object]$Refs,
        [int]$ExpectedFeatureCount
    )

    Assert-Condition -Condition ($null -ne $Refs) -Message (
        $Case.Name + ": C3c-A refs missing (Ensure-C3cARealInputs did not run)")
    Assert-Condition -Condition ($Case.Name -eq "convert_roads_geojson") -Message (
        "Assert-C3cAConvertCase got '" + $Case.Name + "'")

    $geojsonPath = Join-Path $ResolvedOutputRoot "roads_core.geojson"
    $head = (@(Get-Content -Path $geojsonPath -TotalCount 3 -Encoding UTF8) -join " ")
    Assert-Condition -Condition ($head.Contains('"FeatureCollection"')) -Message (
        $Case.Name + ": header is not a FeatureCollection, got: " + $head)

    $inputInfo = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $Refs.RoadsCopy `
        -ScratchDir $Refs.ScratchRoot -Label "convert_input_roads"
    $outputInfo = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $geojsonPath `
        -ScratchDir $Refs.ScratchRoot -Label "convert_output_roads"

    Assert-Condition -Condition ($outputInfo.FeatureCount -eq $ExpectedFeatureCount) -Message (
        $Case.Name + ": output has " + $outputInfo.FeatureCount + " feature(s), roads core reports " + $ExpectedFeatureCount)
    Assert-Condition -Condition ($outputInfo.FeatureCount -eq $inputInfo.FeatureCount) -Message (
        $Case.Name + ": output " + $outputInfo.FeatureCount + " feature(s) != input " + $inputInfo.FeatureCount)
    Assert-Condition -Condition ($outputInfo.FieldCount -eq $inputInfo.FieldCount) -Message (
        $Case.Name + ": output has " + $outputInfo.FieldCount + " field(s), input has " + $inputInfo.FieldCount)
    Assert-Condition -Condition (($outputInfo.FieldNames -join ",") -ceq ($inputInfo.FieldNames -join ",")) -Message (
        $Case.Name + ": field list/order '" + ($outputInfo.FieldNames -join ",") + "' != input '" + ($inputInfo.FieldNames -join ",") + "'")
    Assert-Condition -Condition ($outputInfo.LayerName -ceq $inputInfo.LayerName) -Message (
        $Case.Name + ": output LayerName='" + $outputInfo.LayerName + "', input '" + $inputInfo.LayerName + "'")
    # 几何类型这一条**不比层声明**：GeoJSON 驱动的层类型恒为 Unknown（可装任意几何），
    # 输入 GPKG 是 MultiLineString ⇒ 两个 info 读出来的层声明天然不同，比它等于比格式而非比转换。
    # 逐要素几何的保真由单测 VectorConvertExecution 逐点钉住；真实腿这一层比要素数/字段/CRS/extent。
    Assert-Condition -Condition ($outputInfo.Crs -ceq $inputInfo.Crs) -Message (
        $Case.Name + ": output CRS='" + $outputInfo.Crs + "', input '" + $inputInfo.Crs + "'")

    $largestAbs = 0.0
    foreach ($v in @($inputInfo.MinX, $inputInfo.MaxX, $inputInfo.MinY, $inputInfo.MaxY)) {
        if ([Math]::Abs([double]$v) -gt $largestAbs) { $largestAbs = [Math]::Abs([double]$v) }
    }
    $tolerance = [Math]::Max($largestAbs * $script:C3fExtentRelativeTolerance, 1e-9)
    Assert-ExtentEqualTo -Expected $inputInfo -Actual $outputInfo -Tolerance $tolerance -Label $Case.Name
    $script:C3cAAssertedCount = $script:C3cAAssertedCount + 1
    return $outputInfo
}

# ── C3c-B：simplify 的真实数据腿（roads core 3857，投影线层，两万级要素）────────────
# 输入复制到自己的 scratch 目录再喂 CLI。WHY 不复用 c3cA 那份 roads 复制件：本刀的输入侧期望也是
# 用 vector info 现读的，而 info 会在 GPKG 输入旁边落一个 .aux.xml（N115）⇒ 两份共用的话，
# info_roads 那条"边车必须在场"的判据会被我这次读取代劳，跨用例串证。
function Ensure-C3cBRealInputs {
    param(
        [string]$ResolvedWorkspaceRoot,
        [string]$ResolvedCliPath
    )

    $scratchRoot = Join-Path $ResolvedWorkspaceRoot "build\tmp\c3cB"
    New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

    $roadsBenchmark = Join-Path $ResolvedWorkspaceRoot "tmp\vector_benchmark\bj_roads_core_3857.gpkg"
    Require-Path -Path $roadsBenchmark -Label "C3c-B roads benchmark"
    $roadsCopy = Join-Path $scratchRoot "simplify_roads_input.gpkg"
    Copy-Item $roadsBenchmark $roadsCopy -Force
    Remove-Item ($roadsCopy + ".aux.xml") -Force -ErrorAction SilentlyContinue

    # 输入侧期望一次读齐（要素数 / 字段名与顺序 / 层名 / 几何类族 / CRS / 包框）。读的是复制件自己，
    # 不是任何动作的产物 ⇒ 与 N108 那条"不拿产物比产物"不冲突。
    $inputInfo = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $roadsCopy `
        -ScratchDir $scratchRoot -Label "simplify_input_roads"
    Assert-Condition -Condition ($inputInfo.FeatureCount -ge 2) -Message (
        "C3c-B: roads copy carries " + $inputInfo.FeatureCount + " feature(s), expected a real layer")
    Assert-Condition -Condition ($inputInfo.Crs -ceq "EPSG:3857") -Message (
        "C3c-B: roads copy CRS is '" + $inputInfo.Crs + "', simplify only accepts a projected layer")

    Write-Host ("  c3c-B inputs: roads copy=" + $inputInfo.FeatureCount + " feature(s), crs=" +
        $inputInfo.Crs + ", fields=" + (@($inputInfo.FieldNames) -join ","))

    return [pscustomobject]@{
        ScratchRoot = $scratchRoot
        RoadsCopy = $roadsCopy
        InputInfo = $inputInfo
        # 本腿的开跑下界：造复制件的时刻一定早于任何一次 CLI 调用 ⇒ 产物只有在**本轮**被写过，
        # 它的 LastWriteTimeUtc 才会不早于这个时刻（tmp/vector_regression 的产物跨轮留存，
        # "文件在场"本身不证明这一轮真的写了 —— 本仓吃过"旧件在场、判据全绿"的亏）。
        StartUtc = (Get-Date).ToUniversalTime()
    }
}

function Assert-C3cBSimplifyCase {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [string]$ResolvedOutputRoot,
        [object]$Refs
    )

    Assert-Condition -Condition ($null -ne $Refs) -Message (
        $Case.Name + ": C3c-B refs missing (Ensure-C3cBRealInputs did not run) => expectations would be vacuous")
    Assert-Condition -Condition ($Case.Name -eq "simplify_roads") -Message (
        "Assert-C3cBSimplifyCase got '" + $Case.Name + "'")

    $expected = $Refs.InputInfo
    # 三方对撞：CLI 报的完成数、metadata 的 feature_count、产物回读的要素数，三个都必须等于**输入**要素数。
    # simplify 是要素级恒等映射（一条不许多、一条不许少），所以"与输入相等"是关系式而不是样本常量。
    $reported = Get-ReportedCount -Text $Case.Output -Pattern "Simplify completed: (\d+) features" `
        -Label ($Case.Name + " reported count")
    Assert-Condition -Condition ($reported -eq $expected.FeatureCount) -Message (
        $Case.Name + ": CLI reports " + $reported + " simplified, input has " + $expected.FeatureCount)
    $featureCount = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label ($Case.Name + " feature_count")
    Assert-Condition -Condition ($featureCount -eq $expected.FeatureCount) -Message (
        $Case.Name + ": feature_count=" + $featureCount + ", input has " + $expected.FeatureCount)

    $outputPath = Join-Path $ResolvedOutputRoot "simplify_roads.gpkg"
    # 产物必须晚于本腿开跑时刻（下界由 Refs.StartUtc 给）：上一轮留下的同名文件会让"在场 + 回读"
    # 两条判据一起变成对旧件的取证。红的时候报出两个时刻，不用重跑就知道差了多少。
    $writtenUtc = (Get-Item $outputPath).LastWriteTimeUtc
    Assert-Condition -Condition ($writtenUtc -ge $Refs.StartUtc) -Message (
        $Case.Name + ": output was last written " + $writtenUtc.ToString("o") + ", before this leg started at " +
        $Refs.StartUtc.ToString("o") + " => the file on disk is a leftover, not this run's product")
    $read = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $outputPath `
        -ScratchDir $Refs.ScratchRoot -Label "simplify_roads_output"
    Assert-Condition -Condition ($read.FeatureCount -eq $expected.FeatureCount) -Message (
        $Case.Name + ": output has " + $read.FeatureCount + " feature(s), input has " + $expected.FeatureCount)
    Assert-Condition -Condition (($read.FieldNames -join ",") -ceq (@($expected.FieldNames) -join ",")) -Message (
        $Case.Name + ": output fields '" + ($read.FieldNames -join ",") + "' != input '" +
        (@($expected.FieldNames) -join ",") + "'")
    Assert-Condition -Condition ($read.LayerName -ceq $expected.LayerName) -Message (
        $Case.Name + ": output LayerName='" + $read.LayerName + "', input '" + $expected.LayerName + "'")
    Assert-Condition -Condition ($read.Geometry -ceq $expected.Geometry) -Message (
        $Case.Name + ": output Geometry='" + $read.Geometry + "', input '" + $expected.Geometry + "'")
    Assert-Condition -Condition ($read.Crs -ceq $expected.Crs) -Message (
        $Case.Name + ": output CRS='" + $read.Crs + "', input '" + $expected.Crs + "'")
    # 形状侧唯一一条独立成立的判据：Douglas-Peucker 只**删**顶点、不新增 ⇒ 产物包框必被输入包框包住
    # （容差 0，四边哪一条越界、越了多少由 Assert-ExtentInsideOf 报出来）。
    # ⚠ 本腿不判"顶点数确实变少"：runner 这层的读数只到图层声明，没有顶点计数；那半边由合成夹具
    #   VectorSimplifyExecution 的两条容差臂钉住（大容差整串等于两点线、小容差整串等于原五点线）。
    Assert-ExtentInsideOf -Outer $expected -Inner $read -Tolerance 0.0 -Label $Case.Name
    $script:C3cBAssertedCount = $script:C3cBAssertedCount + 1
    return $read
}

# ── C3h-3：union 路径 A（面 × 面）的真实数据腿 ──────────────────────────────────
# 既有 union_focus 是线 × 面：源层为线、叠加侧全为面 ⇒ vector_plugin_overlay.inc:34-35 的
#   usePracticalMixedUnion 为真 ⇒ 落路径 B；而路径 B 按契约不产出叠加面（同文件 210 行注释原文
#   "union_overlay_pieces 在这一路恒为 0，那是契约不是缺陷"）⇒ 三类互不重叠分块的那条路径 A
#   在真实数据回归里是 0 覆盖，只由合成夹具钉住（GOLD != 真实数据覆盖，口径见 docs/RELEASE.md N128）。
# 本条腿落路径 A 的**实际**依据（评审席 M1 更正）：实现取的是源层 effectiveLayerGeometryType()
#   （vector_plugin_common.inc:421-444）—— 声明类型有维度就直接用，Unknown 层则取**第一个非空要素**的类型。
#   两个混合层喂进来时省层声明 Unknown ⇒ 路径归属由"首个非空要素是不是面"决定，而不是由"层里含不含面"决定；
#   本档因此现读并断言 FirstNonEmptyGeometryType 是面类型（Ensure-UnionPolyInputs 里那条），
#   另有哨兵 ⑥（union_overlay_pieces >= 1）在产物侧兜住"哪天首个要素变成线就退回路径 B"。
# 选材现读（两侧都是混合维度、同 EPSG:4490）：data/vector/中国_省 42 要素（34 MultiPolygon + 8
#   MultiLineString）、data/vector/中国_市 388 要素（375 MultiPolygon + 13 MultiLineString）。
#   喂给 CLI 的是 build/tmp/c3h3/ 里的复制件，data/ 全程只读（N115：边车只可能长在临时区）。
# WHY 单独成一条注册用例（而不是挂进 overlay 那一半）：拆腿时（C3k）overlay 那一半现读 Debug 244.04 s /
#   TIMEOUT 670 s = 2.75 倍余量，再塞进这条 135 s 级的腿会把该半压到 1.77 倍，跌破本仓 2.5 倍的拆腿触发线
#   ⇒ 拆第三条。（C3k-2 起 overlay 那一半已按"重活一分为二"再拆成 overlay_buffer_join / overlay_ops，此段数字留作当时对照。）
# 判据六条，全部是不变式，没有一个数字是从本机产物抄来的：
#   ① stdout 自报数 == 元数据 union_total_pieces == vector info 落盘读数（三段等式，同 C3f 的 ④ 口径）；
#   ② union_input_pieces + union_overlap_pieces + union_overlay_pieces == union_total_pieces
#      （实现 overlay.inc:177 自己声明的不变量，少一类或多一类都红）；
#   ③ 按 source 值把落盘产物三类各数一遍：逐类现读数 == 对应元数据值，且三类之和 == 落盘总数
#      ⇒ 把"记账"与"真写出去的行"焊死，并钉住 source 词表只有 input/both/overlay 这三类；
#   ④ 分块数下界 union_total_pieces >= 源层"几何非空"的要素数（推导写在
#      Test-GeoJsonGeometryNonEmpty 的函数头，这里不重复论证）；
#   ⑤ 建表契约：输出字段数 == 源层字段数 + 叠加层字段数 + 1（附加的标记列）⇒ 两侧整表属性移交；
#   ⑥ 覆盖哨兵 union_overlay_pieces >= 1：它是"这一路是路径 A"的**判据**（路径 B 恒为 0，见上面 210 行），
#      它红不代表产品坏，代表这条腿的选材不再能覆盖路径 A ⇒ 届时换料重立，不许删判据、不许改成 >= 0。
# ⚠ 不许写"期望 641"这类从本机产物抄来的数；不许把面积/坐标写进期望：两侧输入都是地理坐标系
#   （EPSG:4490），面积单位是度²，只能用于减法记账，不进任何判据。

function Test-GeoJsonGeometryNonEmpty {
    param($Node)

    # "几何非空"只问一件事：这棵几何树里有没有出现过坐标数字。OGR 的 IsEmpty() 对
    #   Polygon/MultiPolygon/LineString/MultiLineString 的语义就是"一个点都没有"，所以本函数与实现里
    #   `if (!geom || geom->IsEmpty()) continue;`（overlay.inc:353-355）跳过的正是同一批要素。
    # 判据 ④ 的下界推导（源侧）：路径 A 对每条**几何非空**的源要素只有两种收尾 ——
    #   (a) candidates 循环里第一次拿到非空交集时 consumed 仍为 null ⇒ piece = intersection->clone() 必非空
    #       ⇒ 写出 >= 1 个 both 块（overlay.inc:370-392）；此后 consumed 才可能非空、后续 piece 才可能为空；
    #   (b) 循环结束时 consumed 仍为 null ⇒ remainder = geom->clone() 必非空 ⇒ 写出 1 个 input 块
    #       （overlay.inc:413-429；Difference 返 null 只可能发生在 consumed 非空那一支，即 (a) 已经写过块）。
    #   ⇒ "每条几何非空的源要素至少贡献一块"，于是 total_pieces >= 源层非空要素数 永远成立。
    # 下界为什么不从叠加侧取：叠加独有块允许为 0（叠加面被源面完全吞掉时 raw->IsEmpty() ⇒ 478-480 行
    #   continue，那是契约不是缺口）⇒ 叠加侧给不出任何非平凡下界。
    if ($null -eq $Node) { return $false }
    if ($Node -is [int] -or $Node -is [long] -or $Node -is [double] -or $Node -is [single] -or $Node -is [decimal]) {
        return $true
    }
    # [string] 也满足 IEnumerable，但 PS 的 foreach 过一个字符串给出的是**它自己**（不是逐字符）
    #   ⇒ 少了这一条，"坐标叶子是字符串"这种坏 GeoJSON 会让递归原地自调直到 CallDepthOverflow
    #   （评审席实测触发过）。字符串叶子一律按"不是坐标"处理。
    if ($Node -is [string]) { return $false }
    if ($Node -is [System.Collections.IEnumerable]) {
        foreach ($child in $Node) {
            if (Test-GeoJsonGeometryNonEmpty -Node $child) { return $true }
        }
        return $false
    }
    if ($Node -is [pscustomobject]) {
        foreach ($prop in $Node.PSObject.Properties) {
            # 只沿坐标所在的成员下钻（coordinates；GeometryCollection 用 geometries），
            # 别的成员（type、crs、属性值）不参与"非空"判定。
            if ($prop.Name -eq "coordinates" -or $prop.Name -eq "geometries") {
                if (Test-GeoJsonGeometryNonEmpty -Node $prop.Value) { return $true }
            }
        }
        return $false
    }
    return $false
}

function Get-GeoJsonGeometryHistogram {
    param(
        [string]$Path,
        [string]$Label
    )

    # PS5.1 的 Get-Content 不带 -Encoding 按 ANSI 代码页解码，UTF-8 的中文属性值会把整段 JSON 读坏，
    # 报出来的是"JSON 无效"这类假产品缺陷 ⇒ -Encoding UTF8 是这条腿的前提，不是风格问题（同 C3f 腿）。
    $text = Get-Content -Path $Path -Raw -Encoding UTF8
    Assert-Condition -Condition ($null -ne $text -and $text.Length -gt 0) -Message (
        $Label + ": staged input copy read back empty: " + $Path)
    $doc = $text | ConvertFrom-Json
    Assert-Condition -Condition ($null -ne $doc.features) -Message (
        $Label + ": GeoJSON has no features member: " + $Path)

    $featureCount = 0
    $nonEmptyCount = 0
    $arealCount = 0
    $linearCount = 0
    $pointCount = 0
    $otherCount = 0
    $nullGeomCount = 0
    $firstNonEmptyType = $null
    $typeNames = @()
    foreach ($feature in @($doc.features)) {
        $featureCount++
        $geometry = $feature.geometry
        if ($null -eq $geometry) {
            $nullGeomCount++
            continue
        }
        $typeName = "<absent>"
        if ($null -ne $geometry.type) { $typeName = [string]$geometry.type }
        if ($typeNames -cnotcontains $typeName) { $typeNames += $typeName }
        if ($typeName -match "Polygon") { $arealCount++ }
        elseif ($typeName -match "LineString") { $linearCount++ }
        elseif ($typeName -match "Point") { $pointCount++ }
        else { $otherCount++ }
        # 记下**第一个非空要素**的类型：实现给 Unknown 层定有效几何类型时取的就是它
        #   （vector_plugin_common.inc:431-441），所以"这条腿到底走路径 A 还是 B"由它决定，不由"层里含不含面"决定。
        if (Test-GeoJsonGeometryNonEmpty -Node $geometry) {
            $nonEmptyCount++
            if ($null -eq $firstNonEmptyType) { $firstNonEmptyType = $typeName }
        }
    }

    Assert-Condition -Condition ($featureCount -gt 0) -Message (
        $Label + ": the staged copy exposes 0 feature(s), so this leg would assert nothing")
    # 逐维度分类之并必须等于要素数（抓"有一类没被上面四个分支认领"的漏计 ⇒ 下界与前提都建立在这套计数上）。
    Assert-Condition -Condition (($arealCount + $linearCount + $pointCount + $otherCount + $nullGeomCount) -eq $featureCount) -Message (
        $Label + ": dimension histogram " + $arealCount + "+" + $linearCount + "+" + $pointCount + "+" +
        $otherCount + "+" + $nullGeomCount + " != feature count " + $featureCount)

    return [pscustomobject]@{
        FeatureCount = $featureCount
        NonEmptyGeometryCount = $nonEmptyCount
        ArealCount = $arealCount
        LinearCount = $linearCount
        PointCount = $pointCount
        OtherCount = $otherCount
        NullGeometryCount = $nullGeomCount
        FirstNonEmptyGeometryType = $firstNonEmptyType
        TypeHistogram = (($typeNames | Sort-Object) -join ",")
    }
}

function Resolve-CityVectorInput {
    param(
        [string]$VectorDataRoot,
        [string]$Label
    )

    Require-Path -Path $VectorDataRoot -Label "data/vector"

    # 名字按码点拼而不写字面中文（同 Resolve-ProvinceVectorInput：本 .ps1 是无 BOM 的 UTF-8，
    # PS5.1 的 -File 按 ANSI 代码页解源码，字面中文路径在这台机器上会变成乱码）。
    $cityName = (-join @([char]0x4E2D, [char]0x56FD, [char]0x005F, [char]0x5E02)) + ".geojson"
    $cityRaw = Join-Path $VectorDataRoot $cityName
    # 叠加侧**不许**像省层那样退回"随便一个 geojson"：判据 ④ 与路径 A 的前提都只对着这一对图层成立，
    # 换一个图层就把"面 × 面"悄悄变成别的组合 ⇒ 缺它就是红着说明。
    Assert-Condition -Condition (Test-Path $cityRaw) -Message (
        $Label + ": overlay input " + $cityName + " is absent from " + $VectorDataRoot +
        " (this leg names both layers explicitly; silently swapping in another layer would void " +
        "the path-A precondition and the piece-count lower bound)")

    return $cityRaw
}

function Ensure-UnionPolyInputs {
    param(
        [string]$ResolvedCliPath,
        [string]$ResolvedWorkspaceRoot,
        [string]$ResolvedOutputRoot,
        # 与 Ensure-TopologyCheckInputs 同形：不消费这批派生件的档只取路径来注册（名单不变量要从
        # 全量注册里导），真跑那一半才造件；调用方在没造件时把 refs 置 $null，判据函数见到 $null 直接红。
        [switch]$PathsOnly
    )

    # 边车落点：build/tmp/c3h3（data/ 旁边一个字节都不许写，N115）。
    $scratchRoot = Join-Path $ResolvedWorkspaceRoot "build\tmp\c3h3"
    $sourcePath = Join-Path $scratchRoot "union_source.geojson"
    $overlayPath = Join-Path $scratchRoot "union_overlay.geojson"
    $outputPath = Join-Path $ResolvedOutputRoot "union_poly_focus.gpkg"

    if ($PathsOnly) {
        return [pscustomobject]@{
            ScratchRoot = $scratchRoot
            SourcePath = $sourcePath
            OverlayPath = $overlayPath
            OutputPath = $outputPath
            SourceStats = $null
            OverlayStats = $null
            SourceHistogram = $null
            ExpectedFieldCount = $null
            ClassOutputPaths = $null
        }
    }

    # 复制件一律现造、不缓存（同 Ensure-TopologyCheckInputs 那条口径：缓存会让清过 build 的机器行为
    # 不同，也会把这条腿的耗时估低 —— 而工单要求加完腿之后现跑现报它的墙钟并据此定 TIMEOUT）。
    if (Test-Path $scratchRoot) {
        Remove-Item -Recurse -Force $scratchRoot
    }
    New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

    $vectorDataRoot = Join-Path $ResolvedWorkspaceRoot "data\vector"
    $sourceRaw = Resolve-ProvinceVectorInput -VectorDataRoot $vectorDataRoot -Label "union poly-poly leg"
    $overlayRaw = Resolve-CityVectorInput -VectorDataRoot $vectorDataRoot -Label "union poly-poly leg"
    # 两侧不许是同一个文件：自己并自己没有"叠加独有块"可言，判据 ③④ 会退化成同义反复。
    Assert-Condition -Condition ($sourceRaw -cne $overlayRaw) -Message (
        "union poly-poly leg: source and overlay resolve to the same file " + $sourceRaw)
    Copy-Item $sourceRaw $sourcePath -Force
    Copy-Item $overlayRaw $overlayPath -Force

    $sourceStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $sourcePath -ScratchDir $scratchRoot
    $overlayStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $overlayPath -ScratchDir $scratchRoot
    $histogram = Get-GeoJsonGeometryHistogram -Path $sourcePath -Label "union poly-poly leg (source side)"

    # 下界（判据 ④）要的是"实现真正遍历到的那批要素"，所以先把两个读法对撞：JSON 里的要素数
    # 必须等于 vector info 报的 Features —— 两个读法不一致时下界不可信，先红在这里而不是红在判据上。
    Assert-Condition -Condition ($histogram.FeatureCount -eq $sourceStats.FeatureCount) -Message (
        "union poly-poly leg: the staged copy exposes " + $histogram.FeatureCount + " feature(s) in JSON but " +
        "vector info reports Features: " + $sourceStats.FeatureCount + " (the two readers disagree, so the " +
        "piece-count lower bound cannot be trusted)")
    Assert-Condition -Condition ($histogram.NonEmptyGeometryCount -le $sourceStats.FeatureCount) -Message (
        "union poly-poly leg: non-empty geometry count " + $histogram.NonEmptyGeometryCount +
        " exceeds layer Features " + $sourceStats.FeatureCount)
    # 判据 ④ 必须是非平凡的：源层一条非空几何都没有的话 "total >= 0" 就是恒真断言（本仓在消灭的那一类）。
    Assert-Condition -Condition ($histogram.NonEmptyGeometryCount -ge 1) -Message (
        "union poly-poly leg: source side has " + $histogram.NonEmptyGeometryCount + " feature(s) with a " +
        "non-empty geometry (types [" + $histogram.TypeHistogram + "]), so the lower bound is vacuous")
    # 路径归属的**真前提**（评审席 M1）：Unknown 层的有效几何类型 = 第一个非空要素的类型，所以这条腿走的
    #   是不是路径 A 由它决定。它不是面 ⇒ 源层被判成线类型，叠加侧全为面时就落路径 B ⇒ 本腿名义上在跑、
    #   实际覆盖的是 union_focus 已经覆盖过的那条路 ⇒ 当场红，而不是静默换个路径继续绿。
    Assert-Condition -Condition ($null -ne $histogram.FirstNonEmptyGeometryType -and
        $histogram.FirstNonEmptyGeometryType -match "Polygon") -Message (
        "union poly-poly leg: the first non-empty source feature is [" + $histogram.FirstNonEmptyGeometryType +
        "], and effectiveLayerGeometryType() (vector_plugin_common.inc:421-444) takes exactly that type for a " +
        "layer declared Unknown => the implementation would not be on path A. Path coverage here is positional, " +
        "not a property of the layer as a whole; re-choose the input pair, do not delete this assertion")
    # 两侧有面才谈得上"叠加独有块"（ArealCount 不再被当作路径判据，只是这条不变式的载体）。
    Assert-Condition -Condition ($histogram.ArealCount -ge 1) -Message (
        "union poly-poly leg: source side has no areal geometry (types [" + $histogram.TypeHistogram +
        "]), so path A could not leave any overlay-only piece to read back")
    Assert-Condition -Condition ($overlayStats.FeatureCount -ge 1) -Message (
        "union poly-poly leg: overlay side exposes Features: " + $overlayStats.FeatureCount)
    # 判据 ⑤ 的两个加数都要在场：vector info 不打印 Fields 行时 FieldCount 是 null，
    # 静默跳过 ⑤ 就是"没检查长得像检查通过"，所以在这里就红。
    Assert-Condition -Condition ($null -ne $sourceStats.FieldCount -and $null -ne $overlayStats.FieldCount) -Message (
        "union poly-poly leg: vector info exposed no Fields line for the staged inputs " +
        "(source=" + [string]$sourceStats.FieldCount + ", overlay=" + [string]$overlayStats.FieldCount +
        "), so the output field-count contract cannot be checked")

    $expectedFieldCount = [int]$sourceStats.FieldCount + [int]$overlayStats.FieldCount + 1
    $classOutputPaths = [ordered]@{
        input   = Join-Path $scratchRoot "union_class_input.gpkg"
        both    = Join-Path $scratchRoot "union_class_both.gpkg"
        overlay = Join-Path $scratchRoot "union_class_overlay.gpkg"
    }

    # 现读取证打印（不参与判定）：两侧维度分布与 CRS 声明，红的时候不用重跑就知道是哪一侧变了料。
    Write-Host ("  c3h3 leg: source features=" + $histogram.FeatureCount + " non-empty=" +
        $histogram.NonEmptyGeometryCount + " areal=" + $histogram.ArealCount + " linear=" +
        $histogram.LinearCount + " nullGeom=" + $histogram.NullGeometryCount + " types=[" +
        $histogram.TypeHistogram + "] layerGeom=" + $sourceStats.Geometry + " fields=" +
        $sourceStats.FieldCount + " | overlay features=" + $overlayStats.FeatureCount +
        " layerGeom=" + $overlayStats.Geometry + " fields=" + $overlayStats.FieldCount +
        " | expected output fields=" + $expectedFieldCount)

    return [pscustomobject]@{
        ScratchRoot = $scratchRoot
        SourcePath = $sourcePath
        OverlayPath = $overlayPath
        OutputPath = $outputPath
        SourceStats = $sourceStats
        OverlayStats = $overlayStats
        SourceHistogram = $histogram
        ExpectedFieldCount = $expectedFieldCount
        ClassOutputPaths = $classOutputPaths
    }
}

# C3h-3 这一档的用例名单：注册、判据分发与 GIS_CASE_ROSTER 的第 6 条不变量共用同一份名单。
# 两道闸分工写清（评审席 M2 更正）：
#   "把这条腿错归到别的档"由第 6 条不变量红（三组之并 == full 吃不住这一臂，所以第 6 条必须存在）；
#   "新增一条 union_poly 用例却忘了写判据分支"由 Validate-CaseOutputs 里**按组标分发**那条红
#   —— 那条不认名字，只认 -Group union_poly ⇒ 新用例自动走 Assert-UnionPolyCase，
#     而它内部再核名字是否在本名单里 ⇒ 名单与分发哪个漏了都会点名，不存在"注册了但没人判"的静默通道
#     （原来这个 switch 没有 default，光靠名字分支是会静默全绿的）。名单只有这一处，别处不抄第二份。
$script:UnionPolyCaseNames = @(
    "union_poly_focus"
)
# 判据覆盖计数：Assert-UnionPolyCase 每被调用一次 +1，由第 7 条不变量与"实际跑过的 union_poly 用例数"对撞。
$script:UnionPolyAssertedCount = 0

function Assert-UnionPolyCase {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [object]$Refs
    )

    Assert-Condition -Condition ($null -ne $Refs) -Message (
        $Case.Name + ": the union path-A leg is registered but its input refs are missing " +
        "(Ensure-UnionPolyInputs did not run for this Group) => every expectation would be vacuous")
    Assert-Condition -Condition ($script:UnionPolyCaseNames -ccontains $Case.Name) -Message (
        "Assert-UnionPolyCase got an unexpected case name: " + $Case.Name)
    $script:UnionPolyAssertedCount++

    # ① 第一段：stdout 自报数 == 元数据 union_total_pieces（键取不到即红，整行锚定不吃前缀）。
    $reported = Get-ReportedCount -Text $Case.Output -Pattern "Union completed: (\d+) features" `
        -Label ($Case.Name + " stdout count")
    $totalPieces = Get-ReportedMetadataCount -Text $Case.Output -Key "union_total_pieces" `
        -Label ($Case.Name + " union_total_pieces")
    Assert-Condition -Condition ($reported -eq $totalPieces) -Message (
        $Case.Name + ": stdout says " + $reported + " features but metadata union_total_pieces says " +
        $totalPieces)

    $inputPieces = Get-ReportedMetadataCount -Text $Case.Output -Key "union_input_pieces" `
        -Label ($Case.Name + " union_input_pieces")
    $overlapPieces = Get-ReportedMetadataCount -Text $Case.Output -Key "union_overlap_pieces" `
        -Label ($Case.Name + " union_overlap_pieces")
    $overlayPieces = Get-ReportedMetadataCount -Text $Case.Output -Key "union_overlay_pieces" `
        -Label ($Case.Name + " union_overlay_pieces")

    # ② 三类之和 == 总数（实现 overlay.inc:177 自述的不变量）。
    Assert-Condition -Condition (($inputPieces + $overlapPieces + $overlayPieces) -eq $totalPieces) -Message (
        $Case.Name + ": union_input_pieces(" + $inputPieces + ") + union_overlap_pieces(" + $overlapPieces +
        ") + union_overlay_pieces(" + $overlayPieces + ") != union_total_pieces(" + $totalPieces + ")")

    # 产物可被打开、要素数 > 0，且落盘读数 == 记账 == 自报（① 的第三段）。
    $read = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $Refs.OutputPath `
        -ScratchDir $Refs.ScratchRoot
    Assert-Condition -Condition ($read.FeatureCount -gt 0) -Message (
        $Case.Name + ": output layer reads back " + $read.FeatureCount + " feature(s), expected more than 0")
    Assert-Condition -Condition ($read.FeatureCount -eq $totalPieces) -Message (
        $Case.Name + ": output layer reads back " + $read.FeatureCount + " feature(s) but metadata " +
        "union_total_pieces says " + $totalPieces + " (written rows disagree with the accounting)")

    # ④ 下界：非空源要素至少贡献一块（推导见 Test-GeoJsonGeometryNonEmpty 函数头）。
    $lowerBound = [int]$Refs.SourceHistogram.NonEmptyGeometryCount
    Assert-Condition -Condition ($totalPieces -ge $lowerBound) -Message (
        $Case.Name + ": union_total_pieces " + $totalPieces + " < lower bound " + $lowerBound +
        " (= source-side features with a non-empty geometry, " + $Refs.SourceHistogram.FeatureCount +
        " feature(s) of which " + $Refs.SourceHistogram.NullGeometryCount + " carry a null geometry);" +
        " every non-empty source feature must contribute at least one piece")

    # ⑥ 覆盖哨兵：路径 A 的判据（路径 B 契约上恒为 0）。
    Assert-Condition -Condition ($overlayPieces -ge 1) -Message (
        $Case.Name + ": union_overlay_pieces " + $overlayPieces + " < 1, so this run produced no " +
        "overlay-only piece. Path B never writes any (overlay.inc:210), so either this leg silently fell " +
        "back to path B or the chosen pair no longer leaves any overlay-only area. Either way the leg has " +
        "lost path A coverage: re-choose the input pair, do not delete this assertion.")

    # ③ 三类逐类现读：source 值 → 该类的元数据值。落盘读数只取 Features 行
    #   （某类今天 0 行时 vector info 不打印 Extent 行，走 Get-LayerStats 会红在读取器上而不是红在判据上）。
    $classExpected = [ordered]@{
        input   = $inputPieces
        both    = $overlapPieces
        overlay = $overlayPieces
    }
    $classSum = 0
    foreach ($classValue in @($classExpected.Keys)) {
        $expected = [int]$classExpected[$classValue]
        $classPath = [string]$Refs.ClassOutputPaths[$classValue]
        $filterText = Invoke-CliAndCaptureText -ResolvedCliPath $ResolvedCliPath -Arguments @(
            "vector", "filter",
            ("--input=" + $Refs.OutputPath),
            ("--output=" + $classPath),
            ("--where=source='" + $classValue + "'")
        )
        $filterSelfReport = Get-ReportedMetadataCount -Text $filterText `
            -Key "feature_count" `
            -Label ($Case.Name + " class " + $classValue + " filter feature_count")
        $diskCount = Get-LayerFeatureCount -ResolvedCliPath $ResolvedCliPath -Path $classPath `
            -ScratchDir $Refs.ScratchRoot
        Assert-Condition -Condition ($filterSelfReport -eq $diskCount) -Message (
            $Case.Name + ": class " + $classValue + " filter says " + $filterSelfReport +
            " but the written layer reads back " + $diskCount)
        Assert-Condition -Condition ($diskCount -eq $expected) -Message (
            $Case.Name + ": class " + $classValue + " has " + $diskCount + " row(s) on disk but metadata " +
            "says " + $expected + " (the source column disagrees with the piece accounting)")
        $classSum += $diskCount
    }
    # 三类之并 == 落盘总数 ⇒ source 词表没有第四类取值、也没有留 NULL 的行。
    Assert-Condition -Condition ($classSum -eq $read.FeatureCount) -Message (
        $Case.Name + ": classes sum to " + $classSum + " but the output layer has " + $read.FeatureCount +
        " feature(s) => some rows carry a source value outside input/both/overlay (or NULL)")

    # ⑤ 建表契约：源字段 + 叠加字段 + 标记列（撞名让位只会改名、不会少列）。
    Assert-Condition -Condition ($null -ne $read.FieldCount) -Message (
        $Case.Name + ": vector info exposed no Fields line for the output, so the field contract was skipped")
    Assert-Condition -Condition ($read.FieldCount -eq $Refs.ExpectedFieldCount) -Message (
        $Case.Name + ": output has " + $read.FieldCount + " field(s), expected " + $Refs.ExpectedFieldCount +
        " (= source " + $Refs.SourceStats.FieldCount + " + overlay " + $Refs.OverlayStats.FieldCount +
        " + 1 marker column)")

    return $read
}

function Validate-CaseOutputs {
    param(
        [pscustomobject]$Case,
        [string]$ResolvedCliPath,
        [string]$ResolvedOutputRoot,
        [int]$InputFeatureCount,
        [object]$DerivedRefs,
        [object]$TopologyRefs,
        [object]$ChineseFilterRefs,
        [object]$UnionPolyRefs,
        [object]$C3cARefs,
        [object]$C3cBRefs
    )

    # 期望值一律由输入图层自己的要素数推出，不写死常量。
    # WHY：本脚本自 2026-04-27 起优先用 data/vector 里的真实道路（现 22836 要素），
    # 而旧常量按 3 要素合成样本标定 ⇒ clip/union/intersect/spatial_join 长期红，
    # buffer_quick 则因 "feature_count: 2" 恰是 "feature_count: 22836" 的前缀而假绿。
    # WHY 按**组标**分发而不是按用例名（评审席 M2）：下面那个 switch 没有 default，"New-Case 打了
    #   -Group union_poly、名字也进了 $script:UnionPolyCaseNames，却忘了加判据分支"过去会静默全绿
    #   ⇒ 这条腿的判据覆盖面必须由组标保证，名字只当"这条腿该有什么"的门禁（名单对撞在第 6 条不变量里）。
    if ($Case.Group -eq "union_poly") {
        Assert-UnionPolyCase -Case $Case -ResolvedCliPath $ResolvedCliPath -Refs $UnionPolyRefs | Out-Null
        return
    }

    switch ($Case.Name) {
        "buffer_quick" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected $InputFeatureCount -Label "buffer_quick"
            Assert-TextContains -Text $Case.Output -Expected "srs_type: projected" -Message "buffer_quick srs_type mismatch"
            # C3c-B：这条腿原来只比对 CLI 自报的两个数，一个都没落地读产物 ⇒ 层名/几何类族/CRS/字段
            # 整批丢掉也能整腿绿。现在把"产物侧的层契约"钉住：要素数与输入对撞（独立读端，不是自报数），
            # 层名 = 源层名 + "_buffer"（doBuffer 的 outputLayerName 契约），缓冲后必是 MultiPolygon，
            # CRS 与 9 个字段名及顺序 = 基准件的数据契约（名单见 $script:RoadsCoreFieldNames，只写一处）。
            $bufferOutPath = Join-Path $ResolvedOutputRoot "buffer_quick.gpkg"
            $bufferInfo = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $bufferOutPath `
                -ScratchDir $ResolvedOutputRoot -Label "buffer_quick_output"
            Assert-Condition -Condition ($bufferInfo.FeatureCount -eq $InputFeatureCount) -Message (
                "buffer_quick: output has " + $bufferInfo.FeatureCount + " feature(s), input has " + $InputFeatureCount)
            Assert-Condition -Condition ($bufferInfo.LayerName -ceq ($script:RoadsCoreLayerName + "_buffer")) -Message (
                "buffer_quick: LayerName='" + $bufferInfo.LayerName + "', expected '" + $script:RoadsCoreLayerName + "_buffer'")
            Assert-Condition -Condition ($bufferInfo.Geometry -ceq "MultiPolygon") -Message (
                "buffer_quick: Geometry='" + $bufferInfo.Geometry + "', expected MultiPolygon")
            Assert-Condition -Condition ($bufferInfo.Crs -ceq "EPSG:3857") -Message (
                "buffer_quick: CRS='" + $bufferInfo.Crs + "', expected EPSG:3857")
            Assert-Condition -Condition (($bufferInfo.FieldNames -join ",") -ceq ($script:RoadsCoreFieldNames -join ",")) -Message (
                "buffer_quick: field list/order '" + ($bufferInfo.FieldNames -join ",") + "' != contract '" +
                ($script:RoadsCoreFieldNames -join ",") + "'")
        }
        "clip_focus" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected $InputFeatureCount -Label "clip_focus"
        }
        "difference_focus" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected 0 -Label "difference_focus"
        }
        "intersect_focus" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected $InputFeatureCount -Label "intersect_focus"
        }
        "spatial_join_focus" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected $InputFeatureCount -Label "spatial_join_focus"
            Assert-Condition -Condition (Test-Path (Join-Path $ResolvedOutputRoot "spatial_join_focus.gpkg")) -Message "spatial_join_focus output missing"
            # C3c-C2：这条腿此前只判"CLI 自报的要素数 + 文件在场"。补三条**产物侧**判据，钉的是
            # doSpatialJoin 的三条输出契约（现读 vector_plugin_spatial_join.inc:75-97 与 :150）：
            #   层名 = **源层名原样**（不是重命名）、字段 = 输入 9 个 + join_count（本用例没点 join_field
            #   ⇒ 按契约不该有 join_value，这一条同时挡住"无条件加两个字段"的坏实现）、
            #   **几何不许动**（实现里 dstFeat->SetGeometry(srcGeom)）⇒ 产物包框必须与输入现读的包框逐边相等。
            # 期望来源全是输入侧：要素数 = $InputFeatureCount（由 $roadsStats 现读）、包框 = $roadsStats、
            # 字段名单 = $script:RoadsCoreFieldNames（单点定义，与 info_roads/buffer_quick 同一份）。
            $joinOutPath = Join-Path $ResolvedOutputRoot "spatial_join_focus.gpkg"
            $joinInfo = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $joinOutPath `
                -ScratchDir $ResolvedOutputRoot -Label "spatial_join_focus_output"
            Assert-Condition -Condition ($joinInfo.FeatureCount -eq $InputFeatureCount) -Message (
                "spatial_join_focus: output has " + $joinInfo.FeatureCount + " feature(s), input has " + $InputFeatureCount)
            Assert-Condition -Condition ($joinInfo.LayerName -ceq $script:RoadsCoreLayerName) -Message (
                "spatial_join_focus: LayerName='" + $joinInfo.LayerName + "', expected source name '" +
                $script:RoadsCoreLayerName + "'")
            $expectedJoinFields = @($script:RoadsCoreFieldNames + @("join_count"))
            Assert-Condition -Condition (($joinInfo.FieldNames -join ",") -ceq ($expectedJoinFields -join ",")) -Message (
                "spatial_join_focus: fields '" + ($joinInfo.FieldNames -join ",") + "' != input 9 + join_count '" +
                ($expectedJoinFields -join ",") + "' (this case passes no join_field, so join_value must be absent)")
            Assert-Condition -Condition ($joinInfo.Geometry -ceq "MultiLineString") -Message (
                "spatial_join_focus: Geometry='" + $joinInfo.Geometry + "', expected MultiLineString")
            Assert-Condition -Condition ($joinInfo.Crs -ceq "EPSG:4326") -Message (
                "spatial_join_focus: CRS='" + $joinInfo.Crs + "', expected EPSG:4326 (bj_roads_core is a geographic-CRS layer)")
            $joinLargestAbs = 0.0
            foreach ($v in @($roadsStats.MinX, $roadsStats.MaxX, $roadsStats.MinY, $roadsStats.MaxY)) {
                if ([Math]::Abs([double]$v) -gt $joinLargestAbs) { $joinLargestAbs = [Math]::Abs([double]$v) }
            }
            Assert-ExtentEqualTo -Expected $roadsStats -Actual $joinInfo `
                -Tolerance ([Math]::Max($joinLargestAbs * $script:C3fExtentRelativeTolerance, 1e-9)) `
                -Label "spatial_join_focus"
        }
        "union_focus" {
            # 这条**不**跟着换成 feature_count：doUnion 的计数语义是"分块数"，它自己已经发了
            # union_total_pieces（= 同一个 count，见 vector_plugin_overlay.inc 的 union_* 记账），
            # 再补一个 feature_count 就是同一个数发两个键 ⇒ 正是本刀要清的冗余，不是要铺的口径。
            $unionCount = Get-ReportedCount -Text $Case.Output -Pattern "Union completed: (\d+) features" -Label "union_focus"
            Assert-Condition -Condition ($unionCount -ge $InputFeatureCount) -Message ("union_focus: reported " + $unionCount + ", expected at least " + $InputFeatureCount)
        }
        "clip_far" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected 0 -Label "clip_far"
        }
        "intersect_far" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected 0 -Label "intersect_far"
        }
        "difference_far" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected $InputFeatureCount -Label "difference_far"
        }
        "clip_partial" {
            $partialCount = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label "clip_partial"
            Assert-Condition -Condition ($partialCount -gt 0 -and $partialCount -le $InputFeatureCount) -Message ("clip_partial: reported " + $partialCount + ", expected in (0, " + $InputFeatureCount + "]")
            $script:ClipPartialCount = $partialCount
        }
        "intersect_partial" {
            $partialCount = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" -Label "intersect_partial"
            Assert-Condition -Condition ($partialCount -gt 0 -and $partialCount -le $InputFeatureCount) -Message ("intersect_partial: reported " + $partialCount + ", expected in (0, " + $InputFeatureCount + "]")
            Assert-Condition -Condition ($null -ne $script:ClipPartialCount) -Message "intersect_partial ran before clip_partial, cannot cross-check"
            Assert-Condition -Condition ($script:ClipPartialCount -eq $partialCount) -Message ("clip and intersect disagree on the same partial window: " + $script:ClipPartialCount + " vs " + $partialCount)
        }
        "dissolve_bbox" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected 1 -Label "dissolve_bbox"
            # C3c-B：这条腿原来只抠 CLI 消息里那句 "1 features"，一个都没落地读产物。
            # 输入 china_bbox_3857 是**单个**面、且没点名 dissolve_field ⇒ 走"整体融合"分支，
            # 于是能独立成立（不依赖样本标定常量）的判据是这几条：
            #   产物要素数 == 1（无字段融合的契约就是并成一个）、层名 == doDissolve 写死的 "dissolved"、
            #   几何类族 == MultiPolygon（CreateLayer 写死的声明）、CRS 原样 EPSG:3857、
            #   字段数 == 0（不点名就没有分组键可写）、
            #   **包框与输入逐边相等**（并集不改变点集的外包框 ⇒ 这是关系式，不是拿产物比产物：
            #   比的是"输入侧现读的包框"与"产物包框"）。粒度限制见下面容差的来历。
            $dissolveOutPath = Join-Path $ResolvedOutputRoot "dissolve_bbox.gpkg"
            $dissolveIn = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $chinaBbox3857 `
                -ScratchDir $ResolvedOutputRoot -Label "dissolve_bbox_input"
            $dissolveOut = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $dissolveOutPath `
                -ScratchDir $ResolvedOutputRoot -Label "dissolve_bbox_output"
            Assert-Condition -Condition ($dissolveOut.FeatureCount -eq 1) -Message (
                "dissolve_bbox: output has " + $dissolveOut.FeatureCount + " feature(s), expected 1 (no dissolve_field merges all)")
            Assert-Condition -Condition ($dissolveOut.LayerName -ceq "dissolved") -Message (
                "dissolve_bbox: LayerName='" + $dissolveOut.LayerName + "', expected 'dissolved'")
            Assert-Condition -Condition ($dissolveOut.Geometry -ceq "MultiPolygon") -Message (
                "dissolve_bbox: Geometry='" + $dissolveOut.Geometry + "', expected MultiPolygon")
            Assert-Condition -Condition ($dissolveOut.Crs -ceq "EPSG:3857") -Message (
                "dissolve_bbox: CRS='" + $dissolveOut.Crs + "', expected EPSG:3857")
            Assert-Condition -Condition ($dissolveOut.FieldCount -eq 0) -Message (
                "dissolve_bbox: Fields=" + $dissolveOut.FieldCount + ", expected 0 (no dissolve_field given)")
            # 包框相等用**相对**容差：vector info 的 Extent 只印 6 位有效数字，在 1.28e7 这一档坐标上
            # 粒度就是 ±50 m ⇒ 拿 0 去比会被打印精度判红，那不是缺陷。取坐标量级的 1e-6（≈13 m）以下
            # 无意义，这里用 C3f 那条同族式子（1e-5 × 最大绝对值 ≈ 128 m），够包住印数误差。
            $bboxLargestAbs = 0.0
            foreach ($v in @($dissolveIn.MinX, $dissolveIn.MaxX, $dissolveIn.MinY, $dissolveIn.MaxY)) {
                if ([Math]::Abs([double]$v) -gt $bboxLargestAbs) { $bboxLargestAbs = [Math]::Abs([double]$v) }
            }
            Assert-ExtentEqualTo -Expected $dissolveIn -Actual $dissolveOut `
                -Tolerance ([Math]::Max($bboxLargestAbs * $script:C3fExtentRelativeTolerance, 1e-9)) `
                -Label "dissolve_bbox"
        }
        "convert_roads_geojson" {
            # 判据从"头三行 + 要素数"增强到关系级：输入/输出两边的层名、字段名与顺序、
            # CRS、extent 逐项对撞（输入是 build/tmp/c3cA/ 里的 roads 复制件）。层声明类型不比：
            # GeoJSON 恒为 Unknown，见 Assert-C3cAConvertCase 里的说明。
            # 不用 ConvertFrom-Json 整读：PS 5.1 的 Get-Content 默认按 ANSI 代码页解码，
            # 这份 12 MB / 22836 要素的 UTF-8 GeoJSON 里中文属性值会被读坏，
            # 报的是"JSON 语法无效 (position 230)"这种假产品缺陷。这里只用 info 解析器。
            Assert-C3cAConvertCase -Case $Case -ResolvedCliPath $ResolvedCliPath `
                -ResolvedOutputRoot $ResolvedOutputRoot -Refs $C3cARefs `
                -ExpectedFeatureCount $InputFeatureCount | Out-Null
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
            $polygonizeReported = Get-ReportedMetadataCount -Text $Case.Output -Key "feature_count" `
                -Label "polygonize_bbox_raster feature_count"
            Assert-Condition -Condition ($polygonizeReported -eq 1) -Message (
                "polygonize_bbox_raster: reported feature_count " + $polygonizeReported + ", expected 1")
            # C3c-C3：这条腿此前只抠 CLI 消息里那句 "1 polygons"，产物一个字节都没读。
            # 补四条落地契约（期望来源是 doPolygonize 写死的输出契约，不是样本标定常量）：
            # 层名 polygonized、字段恰好一个且叫 DN、层声明是 Polygon（不是 MultiPolygon/Unknown）、
            # 要素数由**独立读端**再数一遍（与自报的那个 1 对撞 ⇒ 自报数与落盘不一致时红在这里）。
            # ⚠ 输入是 rasterize_bbox 那一步的产物（栅格⇒矢量的链路本来就串在产品里），
            #   所以这里**不**拿"输入包框"当期望（那是拿产物比产物）；包框方向的判据由合成夹具承重，
            #   那里两个波段是不同的手推划分，能判出 band 被忽略与 CRS 漏抄（判别见发布台账）。
            $polygonizeOutPath = Join-Path $ResolvedOutputRoot "bbox_polygonized.gpkg"
            $polygonizeInfo = Get-LayerInfoForPath -ResolvedCliPath $ResolvedCliPath -Path $polygonizeOutPath `
                -ScratchDir $ResolvedOutputRoot -Label "polygonize_bbox_raster_output"
            Assert-Condition -Condition ($polygonizeInfo.LayerName -ceq "polygonized") -Message (
                "polygonize_bbox_raster: LayerName='" + $polygonizeInfo.LayerName + "', expected 'polygonized'")
            Assert-Condition -Condition ($polygonizeInfo.Geometry -ceq "Polygon") -Message (
                "polygonize_bbox_raster: Geometry='" + $polygonizeInfo.Geometry + "', expected Polygon")
            Assert-Condition -Condition (($polygonizeInfo.FieldNames -join ",") -ceq "DN") -Message (
                "polygonize_bbox_raster: fields '" + ($polygonizeInfo.FieldNames -join ",") + "', expected exactly 'DN'")
            Assert-Condition -Condition ($polygonizeInfo.FeatureCount -eq $polygonizeReported) -Message (
                "polygonize_bbox_raster: read-back Features=" + $polygonizeInfo.FeatureCount +
                " != reported feature_count=" + $polygonizeReported)
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
        # ── C3f / N127：中文属性值 --where 的真实数据腿（判据见 Ensure-ChineseAttributeFilterInput 上方注释）──
        "filter_cn_attribute" {
            Assert-ChineseAttributeFilterCase -Case $Case -ResolvedCliPath $ResolvedCliPath `
                -Refs $ChineseFilterRefs | Out-Null
        }
        # ── C3c-A：info / repair 的真实数据腿（判据见各 Assert-C3cA* 函数头）──
        "info_province" {
            Assert-C3cAInfoProvinceCase -Case $Case -Refs $C3cARefs | Out-Null
        }
        "info_roads" {
            Assert-C3cAInfoRoadsCase -Case $Case -Refs $C3cARefs `
                -ExpectedFeatureCount $InputFeatureCount | Out-Null
        }
        "repair_province" {
            Assert-C3cARepairCase -Case $Case -ResolvedCliPath $ResolvedCliPath `
                -ResolvedOutputRoot $ResolvedOutputRoot -Refs $C3cARefs | Out-Null
        }
        # ── C3c-B：simplify 的真实数据腿（判据见 Assert-C3cBSimplifyCase 函数头）──
        "simplify_roads" {
            Assert-C3cBSimplifyCase -Case $Case -ResolvedCliPath $ResolvedCliPath `
                -ResolvedOutputRoot $ResolvedOutputRoot -Refs $C3cBRefs | Out-Null
        }
        "clip_stress" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected $InputFeatureCount -Label "clip_stress"
        }
        "difference_stress" {
            Assert-ReportedMetadataCountEquals -Text $Case.Output -Key "feature_count" -Expected 0 -Label "difference_stress"
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

# ── C3c-A：两份真实输入先复制到 build/tmp/c3cA/（N115），输入侧事实一次算齐 ──
$script:C3cARefs = Ensure-C3cARealInputs -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot

# ── C3c-B：simplify 的输入复制件（build/tmp/c3cB/），输入侧事实一次算齐 ──
# 只在真会跑 derive_heavy 那一档（或手工 all）时才造复制件并现读输入：其余四档不付这份
# 7.5 MB 复制 + 一次 22 836 要素 info 现读的钱（与 $buildUnionPolyInputs 那条闸同口径）。
$buildDeriveHeavyInputs = ($Mode -eq "full") -and ($Group -eq "all" -or $Group -eq "derive_heavy")
if ($buildDeriveHeavyInputs) {
    $script:C3cBRefs = Ensure-C3cBRealInputs -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -ResolvedCliPath $ResolvedCliPath
} else {
    # 置 $null 而不是"有路径、没现读属性"的半成品：某条腿被归错档却真跑起来时，红在
    # Assert-C3cBSimplifyCase 开头那条 null 判据上，而不是拿 $null 的属性比较蒙成绿。
    $script:C3cBRefs = $null
}

# 三个窗口按输入图层自己的范围生成，因此对"合成样本"和"data/vector 真实道路"都成立：
#   all     —— 外扩 20%，必须完全覆盖输入 ⇒ clip/intersect/ spatial_join 是恒等、difference 是空；
#   far     —— 整块挪出范围之外 ⇒ 三个叠加动作必须一个都不留 / 一个都不少；
#   partial —— 取范围中间的 25%~75% ⇒ 真正会切到几何，用来钉住 clip 与 intersect 的一致性。
$roadsStats = Get-LayerStats -ResolvedCliPath $ResolvedCliPath -Path $roadsCore -ScratchDir $ResolvedOutputRoot
$InputFeatureCount = $roadsStats.FeatureCount
# bj_roads_core_3857 那份基准件的**数据契约**（层名 input、9 个字段与顺序、EPSG:3857）：名单只写这一处，
# info_roads 与 buffer_quick 两条腿各自对撞它 —— 两处各抄一份正是本仓反复在防的"手抄名单漂移"。
$script:RoadsCoreLayerName = "input"
$script:RoadsCoreFieldNames = @("fclass", "fclass_CN", "name", "ref", "oneway", "maxspeed", "layer", "bridge", "tunnel")
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

# ── C3k：档（Group）与造件闸门 ────────────────────────────────────────────────
# 拆成多条注册用例之后，每一档只造自己消费的派生件：checks 消费 w212/buf30/prov3857/
# province_boundary/ring_endpoints（Ensure-TopologyCheckInputs），overlay_buffer_join / overlay_ops
# （C3k-2 把原 overlay 一分为二的四条腿里的两条）都消费 roadsCore + allWindow 这批早于它就造好的件
# 与 C3a/C3f 的切片、省层复制件（Ensure-DerivedGeometryInputs），union_poly（C3h-3 加的第四条腿）
# 只消费 build/tmp/c3h3/ 里那两个省/市复制件。
# 各档都**照常注册全部用例**（名单不变量要从完整 $cases 导全量，见下面 GIS_CASE_ROSTER 那段），
# 过滤只发生在派发那一步。
# quick 档不分档（27 条用例各档都跑，Group=all）：-GROUP 写成 checks/overlay_buffer_join/overlay_ops/
# union_poly/derive_heavy 配 Mode=quick 会筛出 0 条用例 => 整条腿"退出码 0 但什么都没判"，所以这里先把它判红。
if ($Mode -eq "quick" -and $Group -ne "all") {
    throw ("Mode=quick has no group split: pass -Group all (got Mode=" + $Mode + " Group=" + $Group + ")")
}
$buildOverlayInputs = ($Group -eq "all" -or $Group -eq "overlay_buffer_join" -or $Group -eq "overlay_ops")
$buildCheckInputs = ($Mode -eq "full") -and ($Group -eq "all" -or $Group -eq "checks")
# C3h-3：第三条腿只消费自己的两个复制件（build/tmp/c3h3/），所以造件闸门同形；
# 它整批只在 Mode=full 注册 ⇒ quick 档（real_data_vector_quick，TIMEOUT 720 s）不会被这条 135 s 级的腿压穿。
$buildUnionPolyInputs = ($Mode -eq "full") -and ($Group -eq "all" -or $Group -eq "union_poly")

$cases = @()
# ── C3k-2：原 overlay 一档按"重活一分为二"拆成两条腿（只挪组标，判据一字不动）─────────────
# 分组依据是 2026-10-23 收口轮 tmp/vector_regression/summary_full_overlay.csv 的逐用例现读
# （26 条合计 256.93 s）：
#   overlay_buffer_join（C3k-2 时 15 条，现读合计 152.53 s；C3c-A 加 info_province / info_roads /
#     repair_province 三条后为 18 条）= buffer_quick 64.54 + spatial_join_focus 55.66
#     + 所有 <=5 s 的几何派生/转换小用例 17.70 + vertices_line 14.63；
#   overlay_ops（11 条，现读合计 104.40 s）= clip/difference/union/intersect 的 focus/far/partial/stress。
# 两条腿都消费 roadsCore/allWindow 与 C3a 派生件 ⇒ $buildOverlayInputs 对两组建真（见上面的闸）。
$cases += New-Case -Name "buffer_quick" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "buffer",
    ("--input=" + $roadsCore3857),
    ("--output=" + (Join-Path $ResolvedOutputRoot "buffer_quick.gpkg")),
    "--distance=100"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "buffer_quick.gpkg")
)
$cases += New-Case -Name "clip_focus" -Group "overlay_ops" -CaseArgs @(
    "vector", "clip",
    ("--input=" + $roadsCore),
    ("--clip_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "clip_focus.gpkg")
)
$cases += New-Case -Name "difference_focus" -Group "overlay_ops" -CaseArgs @(
    "vector", "difference",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "difference_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "difference_focus.gpkg")
)
$cases += New-Case -Name "union_focus" -Group "overlay_ops" -CaseArgs @(
    "vector", "union",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "union_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "union_focus.gpkg")
)
$cases += New-Case -Name "intersect_focus" -Group "overlay_ops" -CaseArgs @(
    "vector", "intersect",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "intersect_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "intersect_focus.gpkg")
)
$cases += New-Case -Name "clip_far" -Group "overlay_ops" -CaseArgs @(
    "vector", "clip",
    ("--input=" + $roadsCore),
    ("--clip_vector=" + $farWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_far.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "clip_far.gpkg")
)
$cases += New-Case -Name "intersect_far" -Group "overlay_ops" -CaseArgs @(
    "vector", "intersect",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $farWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "intersect_far.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "intersect_far.gpkg")
)
$cases += New-Case -Name "difference_far" -Group "overlay_ops" -CaseArgs @(
    "vector", "difference",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $farWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "difference_far.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "difference_far.gpkg")
)
$cases += New-Case -Name "clip_partial" -Group "overlay_ops" -CaseArgs @(
    "vector", "clip",
    ("--input=" + $roadsCore),
    ("--clip_vector=" + $partialWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "clip_partial.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "clip_partial.gpkg")
)
$cases += New-Case -Name "intersect_partial" -Group "overlay_ops" -CaseArgs @(
    "vector", "intersect",
    ("--input=" + $roadsCore),
    ("--overlay_vector=" + $partialWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "intersect_partial.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "intersect_partial.gpkg")
)
$cases += New-Case -Name "spatial_join_focus" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "spatial_join",
    ("--input=" + $roadsCore),
    ("--join_vector=" + $allWindow),
    ("--output=" + (Join-Path $ResolvedOutputRoot "spatial_join_focus.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "spatial_join_focus.gpkg")
)
$cases += New-Case -Name "dissolve_bbox" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "dissolve",
    ("--input=" + $chinaBbox3857),
    ("--output=" + (Join-Path $ResolvedOutputRoot "dissolve_bbox.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "dissolve_bbox.gpkg")
)
$cases += New-Case -Name "convert_roads_geojson" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "convert",
    ("--input=" + $script:C3cARefs.RoadsCopy),
    ("--output=" + (Join-Path $ResolvedOutputRoot "roads_core.geojson")),
    "--format=GeoJSON"
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "roads_core.geojson")
)
$rasterizeOutput = Join-Path $ResolvedOutputRoot "bbox_rasterized.tif"
$cases += New-Case -Name "rasterize_bbox" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "rasterize",
    ("--input=" + $chinaBbox),
    ("--output=" + $rasterizeOutput),
    "--resolution=0.05"
) -ExpectedOutputs @(
    $rasterizeOutput
)
$cases += New-Case -Name "polygonize_bbox_raster" -Group "overlay_buffer_join" -CaseArgs @(
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
# C3k-2：这 8 条腿归 overlay_buffer_join 档（vertices 与 midpoints/endpoints 同组 ⇒ 组内顺序即依赖不变），
# checks 那一半只要路径来注册（-PathsOnly），不替它造切片；
# 造没造都由下面 $derivedRefs/$topologyRefs 的置空判据把关，归错档跑起来必红。
$derivedRefs = Ensure-DerivedGeometryInputs -ResolvedCliPath $ResolvedCliPath `
    -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -RoadsCore3857 $roadsCore3857 `
    -PathsOnly:(-not $buildOverlayInputs)
$script:DerivedVertexCount = -1
$script:DerivedMidpointCount = -1
$script:DerivedEndpointCount = -1

function New-DerivedCase {
    param(
        [string]$Name,
        [string]$Action,
        [string]$InputPath,
        [string]$OutputPath,
        [string]$Group
    )

    return New-Case -Name $Name -CaseArgs @(
        "vector", $Action,
        ("--input=" + $InputPath),
        ("--output=" + $OutputPath)
    ) -ExpectedOutputs @($OutputPath) -Group $Group
}

$cases += New-DerivedCase -Name "hull_polygon" -Group "overlay_buffer_join" -Action "convex_hull" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["hull_polygon"]
$cases += New-DerivedCase -Name "envelope_polygon" -Group "overlay_buffer_join" -Action "envelope" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["envelope_polygon"]
$cases += New-DerivedCase -Name "centroid_polygon" -Group "overlay_buffer_join" -Action "centroid" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["centroid_polygon"]
$cases += New-DerivedCase -Name "interior_polygon" -Group "overlay_buffer_join" -Action "interior_point" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["interior_polygon"]
$cases += New-DerivedCase -Name "boundary_polygon" -Group "overlay_buffer_join" -Action "boundary" `
    -InputPath $derivedRefs.PolygonInput -OutputPath $derivedRefs.Outputs["boundary_polygon"]
# 顺序即依赖：vertices 先跑，midpoints/endpoints 的关系判据要用它的现读数。
$cases += New-DerivedCase -Name "vertices_line" -Group "overlay_buffer_join" -Action "vertices_extract" `
    -InputPath $derivedRefs.LineSlice -OutputPath $derivedRefs.Outputs["vertices_line"]
$cases += New-DerivedCase -Name "midpoints_line" -Group "overlay_buffer_join" -Action "midpoints_extract" `
    -InputPath $derivedRefs.LineSlice -OutputPath $derivedRefs.Outputs["midpoints_line"]
$cases += New-DerivedCase -Name "endpoints_line" -Group "overlay_buffer_join" -Action "endpoints_extract" `
    -InputPath $derivedRefs.LineSlice -OutputPath $derivedRefs.Outputs["endpoints_line"]

# ── C3f / N127：中文属性值的 --where 真实数据腿（quick/full 两档都跑）──
# 输入是 data/vector/中国_省.geojson 的复制件；目标值与 M 在注册前从输入自己的 JSON 现读，
# 判据（④⑤⑥⑦⑧）见 Ensure-ChineseAttributeFilterInput 上方的注释块。
$script:ChineseFilterRefs = Ensure-ChineseAttributeFilterInput -ResolvedCliPath $ResolvedCliPath `
    -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot
$cases += New-Case -Name "filter_cn_attribute" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "filter",
    ("--input=" + $script:ChineseFilterRefs.InputPath),
    ("--output=" + $script:ChineseFilterRefs.OutputPath),
    ("--where=" + $script:ChineseFilterRefs.Where)
) -ExpectedOutputs @(
    $script:ChineseFilterRefs.OutputPath
)

# ── C3c-A：info（省层 + 道路层）与 repair（省层）的真实数据 quick 用例 ──────────────
# info 不写产物，但 D1 未销账时插件级 output 仍必填 ⇒ 带一个不会落盘的占位输出（同 Get-LayerStats）。
$cases += New-Case -Name "info_province" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "info",
    ("--input=" + $script:C3cARefs.ProvinceCopy),
    ("--output=" + (Join-Path $ResolvedOutputRoot "info_province_placeholder.gpkg"))
) -ExpectedOutputs @()
$cases += New-Case -Name "info_roads" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "info",
    ("--input=" + $script:C3cARefs.RoadsCopy),
    ("--output=" + (Join-Path $ResolvedOutputRoot "info_roads_placeholder.gpkg"))
) -ExpectedOutputs @()
$cases += New-Case -Name "repair_province" -Group "overlay_buffer_join" -CaseArgs @(
    "vector", "repair",
    ("--input=" + $script:C3cARefs.ProvinceCopy),
    ("--output=" + (Join-Path $ResolvedOutputRoot "repair_province.gpkg"))
) -ExpectedOutputs @(
    (Join-Path $ResolvedOutputRoot "repair_province.gpkg")
)

# quick 档整批不注册这批腿、overlay 两条腿也不消费它（工单：11 个动作 18 条腿归 real_data_vector_checks
# 那一档）⇒ 先置 $null，让"用例被单独抽出来跑"落在 Assert-TopologyCheckCase 的显式判红上，
# 而不是变量不存在。下面按档置空的那一步与这条判红是同一件事。
$topologyRefs = $null
# C3h-3 同形：union 路径 A 那条腿（1 条用例，归 real_data_vector_union_poly 那一档）在别的档里只取路径注册，
# 没造复制件时置 $null ⇒ 归错档却真跑起来会红在 Assert-UnionPolyCase 的 null 判据上。
$unionPolyRefs = $null

if ($Mode -eq "full") {
    # C3c-B 的名单：注册、判据分发与下面两条不变量（正向对撞 + 组标门禁）共用这一份，不手抄第二遍
    # （本仓出过"手抄名单漂移 ⇒ 新条目不被断言"的事故）。
    $script:C3cBCaseNames = @("simplify_roads")
    # ── C3c-B：simplify 的真实数据腿（roads core 3857 复制件，两万级要素逐条走 GEOS）──────
    # 单独成档（derive_heavy）而不是挂进 overlay_buffer_join：现读整腿 250.11 s ÷ TIMEOUT 630 = 2.52×，
    # 贴着本仓 2.5× 的拆腿触发线（同腿 Debug 散布几十秒级 ⇒ 再有一次就可能跌破），而抬 TIMEOUT
    # 不是选项（TIMEOUT 是本仓唯一的挂起执法者）。规矩是"新腿按 >=4× 起步、跌破就拆"。
    # quick 档也不挂：real_data_vector_quick 现读余量 2.81×，同样压不起这条 54.22 s 的用例。
    $cases += New-Case -Name "simplify_roads" -Group "derive_heavy" -CaseArgs @(
        "vector", "simplify",
        ("--input=" + $script:C3cBRefs.RoadsCopy),
        ("--output=" + (Join-Path $ResolvedOutputRoot "simplify_roads.gpkg")),
        "--tolerance=2.0"
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "simplify_roads.gpkg")
    )
    $cases += New-Case -Name "clip_stress" -Group "overlay_ops" -CaseArgs @(
        "vector", "clip",
        ("--input=" + $roadsCore),
        ("--clip_vector=" + $allWindow),
        ("--output=" + (Join-Path $ResolvedOutputRoot "clip_stress.gpkg"))
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "clip_stress.gpkg")
    )
    $cases += New-Case -Name "difference_stress" -Group "overlay_ops" -CaseArgs @(
        "vector", "difference",
        ("--input=" + $roadsCore),
        ("--overlay_vector=" + $allWindow),
        ("--output=" + (Join-Path $ResolvedOutputRoot "difference_stress.gpkg"))
    ) -ExpectedOutputs @(
        (Join-Path $ResolvedOutputRoot "difference_stress.gpkg")
    )
    # ── C3b-A：vector 拓扑/检查批 11 动作的真实数据腿（18 条用例；归 real_data_vector_checks 那一档）──
    # 派生件与判据的算式都写在 Ensure-TopologyCheckInputs / Assert-TopologyCheckCase 的注释块里。
    # overlay 两条腿只取路径注册、不造这批件（-PathsOnly），见函数头的说明。
    $topologyRefs = Ensure-TopologyCheckInputs -ResolvedCliPath $ResolvedCliPath `
        -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -RoadsCore3857 $roadsCore3857 `
        -PathsOnly:(-not $buildCheckInputs)
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

    # ── C3h-3：union 路径 A（面 × 面）的真实数据腿（1 条用例，归 real_data_vector_union_poly 那一档）──
    # 判据六条与分块数下界的推导都写在 Ensure-UnionPolyInputs / Assert-UnionPolyCase 上方的注释块里。
    # 只在 Mode=full 注册：real_data_vector_quick（TIMEOUT 720 s，现读 236.47 s ⇒ 3.05 倍）不注册这条腿，
    # 否则 quick 那一半会被这条 135 s 级的腿压到 1.94 倍，同样跌破拆腿触发线。
    # 其余两档只取路径注册（-PathsOnly），不替这条腿造复制件；见函数头的说明。
    $unionPolyRefs = Ensure-UnionPolyInputs -ResolvedCliPath $ResolvedCliPath `
        -ResolvedWorkspaceRoot $ResolvedWorkspaceRoot -ResolvedOutputRoot $ResolvedOutputRoot `
        -PathsOnly:(-not $buildUnionPolyInputs)
    $cases += New-Case -Name "union_poly_focus" -Group "union_poly" -CaseArgs @(
        "vector", "union",
        ("--input=" + $unionPolyRefs.SourcePath),
        ("--overlay_vector=" + $unionPolyRefs.OverlayPath),
        ("--output=" + $unionPolyRefs.OutputPath)
    ) -ExpectedOutputs @(
        $unionPolyRefs.OutputPath
    )
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
# C3c-A 的"跑过数 == 判过数"计数器：四个断言函数各 +1，主循环后与真实跑过的用例名对撞。
# 与 union_poly 的第 7 条同形 —— 分发按用例名走，漏掉一个 case 分支时"跑了但没人判"必须红。
$script:C3cAAssertedCount = 0
# C3c-B 同形计数器（跑过数 == 判过数）。
$script:C3cBAssertedCount = 0

# 名单与注册表对撞（正向）：名单里的每一条都必须真的注册成用例；quick 档整批不注册 ⇒ 只在 full 档核条数。
# 反向的"进了名单却没有判据分支"由 Assert-TopologyCheckCase 的 default 负责，两侧都堵住才是闭环。
# C3k 拆腿之后这条判据每一档都跑（注册永远是全量，过滤只发生在派发）⇒ "18 条 checks 腿都注册了"
# 在任何一半里单独都判得出来，不必把两个进程的输出凑起来才看得见。
if ($Mode -eq "full") {
    $registered = @($cases | Where-Object { $script:TopologyCheckCaseNames -contains $_.Name })
    Assert-Condition -Condition ($registered.Count -eq $script:TopologyCheckCaseNames.Count) -Message (
        "C3b-A roster has " + $script:TopologyCheckCaseNames.Count + " name(s), but " + $registered.Count + " case(s) got registered")

    # C3h-3 同形的正向对撞：union 路径 A 名单里的每一条都必须真的注册成用例（反向由
    # Assert-UnionPolyCase 开头的名单门禁负责 ⇒ 两侧都堵住才是闭环，同上面 C3b-A 那两条）。
    $registeredUnionPoly = @($cases | Where-Object { $script:UnionPolyCaseNames -contains $_.Name })
    Assert-Condition -Condition ($registeredUnionPoly.Count -eq $script:UnionPolyCaseNames.Count) -Message (
        "C3h-3 roster has " + $script:UnionPolyCaseNames.Count + " name(s), but " + $registeredUnionPoly.Count +
        " case(s) got registered")

    # C3c-B 同形的正向对撞：derive_heavy 名单里的每一条都必须真的注册成用例
    # （反向的"注册了却没被判"由跑过数==判过数那条，"注册了但组标错"由下面的组标门禁）。
    $registeredC3cB = @($cases | Where-Object { $script:C3cBCaseNames -contains $_.Name })
    Assert-Condition -Condition ($registeredC3cB.Count -eq $script:C3cBCaseNames.Count) -Message (
        "C3c-B roster has " + $script:C3cBCaseNames.Count + " name(s), but " + $registeredC3cB.Count +
        " case(s) got registered")
}

# ── C3k：各分组之并 == 拆分前的全量（本刀承重判据，只在 Mode=full 启用）───────────────
# 名单一律由脚本自己导：full = 当前 $cases 里 Mode=full 注册进来的全部用例名，
# checks/overlay_buffer_join/overlay_ops/union_poly/derive_heavy = 每条用例注册时打的组标。手抄一份名单进脚本等于没写这条判据
# （本仓出过"手抄名单漂移 ⇒ 新条目不被断言"的事故），所以这里不写任何组内名单字面量。
# WHY 各档各自都算完整的全套：注册永远是全量，所以任何一半单独跑都能判出"档被拆坏了"。
if ($Mode -eq "full") {
    # 组名只写这一处：ValidateSet（param 里那份）之外，下面的导名单、判红消息都从这个数组来，
    # 加档时不必再逐条改判据（C3h-3 就是把两两交集换成"按重数数"的同一条式子才做到的；C3k-2 把
    # overlay 一分为二时，这里只加两个名字与两条导名单）。
    $rosterGroupNames = @("checks", "overlay_buffer_join", "overlay_ops", "union_poly", "derive_heavy")
    $rosterFull = @($cases | ForEach-Object { $_.Name })
    $rosterChecks = @($cases | Where-Object { $_.Group -eq "checks" } | ForEach-Object { $_.Name })
    $rosterBufferJoin = @($cases | Where-Object { $_.Group -eq "overlay_buffer_join" } | ForEach-Object { $_.Name })
    $rosterOps = @($cases | Where-Object { $_.Group -eq "overlay_ops" } | ForEach-Object { $_.Name })
    $rosterUnionPoly = @($cases | Where-Object { $_.Group -eq "union_poly" } | ForEach-Object { $_.Name })
    $rosterDeriveHeavy = @($cases | Where-Object { $_.Group -eq "derive_heavy" } | ForEach-Object { $_.Name })
    $rosterUngrouped = @($cases | Where-Object { $rosterGroupNames -cnotcontains $_.Group } |
        ForEach-Object { $_.Name + " (Group=[" + [string]$_.Group + "])" })
    # 不重：同一条腿被两个组同时认领 ⇒ 在"五组之并"里数重数（对任意档数都成立，不必两两求交集）。
    $rosterTaggedAll = @($rosterChecks + $rosterBufferJoin + $rosterOps + $rosterUnionPoly + $rosterDeriveHeavy)
    $rosterBoth = @($rosterTaggedAll | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { [string]$_.Name })
    $rosterFullSorted = @($rosterFull | Sort-Object)
    $rosterChecksSorted = @($rosterChecks | Sort-Object)
    $rosterBufferJoinSorted = @($rosterBufferJoin | Sort-Object)
    $rosterOpsSorted = @($rosterOps | Sort-Object)
    $rosterUnionPolySorted = @($rosterUnionPoly | Sort-Object)
    $rosterDeriveHeavySorted = @($rosterDeriveHeavy | Sort-Object)
    $rosterUnionSorted = @(($rosterChecksSorted + $rosterBufferJoinSorted + $rosterOpsSorted + $rosterUnionPolySorted +
        $rosterDeriveHeavySorted) | Sort-Object)

    # 第 4 条：无论成败先打六行机器可取的名单 ⇒ 判红那一轮也拿得到名单（键名与组名同为 ASCII）。
    Write-Host ("GIS_CASE_ROSTER[full]: " + ($rosterFullSorted -join ","))
    Write-Host ("GIS_CASE_ROSTER[checks]: " + ($rosterChecksSorted -join ","))
    Write-Host ("GIS_CASE_ROSTER[overlay_buffer_join]: " + ($rosterBufferJoinSorted -join ","))
    Write-Host ("GIS_CASE_ROSTER[overlay_ops]: " + ($rosterOpsSorted -join ","))
    Write-Host ("GIS_CASE_ROSTER[union_poly]: " + ($rosterUnionPolySorted -join ","))
    Write-Host ("GIS_CASE_ROSTER[derive_heavy]: " + ($rosterDeriveHeavySorted -join ","))

    # 第 1 条：Group 缺失（或取了 $rosterGroupNames 之外的值）的 full 用例 ⇒ 点名判红。
    Assert-Condition -Condition ($rosterUngrouped.Count -eq 0) -Message (
        "C3k group invariant: " + $rosterUngrouped.Count + " Mode=full case(s) carry no valid Group " +
        "(each case must be tagged one of " + ($rosterGroupNames -join "/") + "): " + ($rosterUngrouped -join "; "))

    # 第 2 条：同一名字同时进两个及以上分组 ⇒ 判红。
    Assert-Condition -Condition ($rosterBoth.Count -eq 0) -Message (
        "C3k group invariant: " + $rosterBoth.Count + " case name(s) got tagged into more than one group: " +
        ($rosterBoth -join ","))

    # 第 3 条：五组之并与全量逐名不等 ⇒ 判红，消息里带六个名单与六个条数。
    $rosterDiff = @()
    $rosterSlots = [Math]::Max($rosterUnionSorted.Count, $rosterFullSorted.Count)
    for ($i = 0; $i -lt $rosterSlots; $i++) {
        $unionName = "<absent>"
        if ($i -lt $rosterUnionSorted.Count) { $unionName = [string]$rosterUnionSorted[$i] }
        $fullName = "<absent>"
        if ($i -lt $rosterFullSorted.Count) { $fullName = [string]$rosterFullSorted[$i] }
        if ($unionName -cne $fullName) {
            $rosterDiff += ("slot " + $i + " union=[" + $unionName + "] full=[" + $fullName + "]")
        }
    }
    Assert-Condition -Condition ($rosterDiff.Count -eq 0) -Message (
        "C3k group invariant: sort(checks)+sort(overlay_buffer_join)+sort(overlay_ops)+sort(union_poly)+sort(derive_heavy) != sort(full) at " + $rosterDiff.Count +
        " name slot(s): " + ($rosterDiff -join " | ") +
        " || full(" + $rosterFullSorted.Count + "): " + ($rosterFullSorted -join ",") +
        " || checks(" + $rosterChecksSorted.Count + "): " + ($rosterChecksSorted -join ",") +
        " || overlay_buffer_join(" + $rosterBufferJoinSorted.Count + "): " + ($rosterBufferJoinSorted -join ",") +
        " || overlay_ops(" + $rosterOpsSorted.Count + "): " + ($rosterOpsSorted -join ",") +
        " || union_poly(" + $rosterUnionPolySorted.Count + "): " + ($rosterUnionPolySorted -join ",") +
        " || derive_heavy(" + $rosterDeriveHeavySorted.Count + "): " + ($rosterDeriveHeavySorted -join ","))

    # 本刀自己加的第 5 条：上面三条都抓不到"把某条 checks 腿的 Group 改成 overlay_buffer_join/overlay_ops"——改档不动
    # 各分组之并，第 1/2/3 条会照样绿（工单 §5 要求的正是这种破坏必须红）。checks 这一档的成员
    # 就是既有的 C3b-A 名单 $script:TopologyCheckCaseNames（它同时是判据分发的门禁，不是本轮
    # 新抄的名单），所以组标必须与它逐名相等，多一条少一条都红。
    $rosterChecksExtra = @($rosterChecksSorted | Where-Object { $script:TopologyCheckCaseNames -notcontains $_ })
    $rosterChecksMissing = @($script:TopologyCheckCaseNames | Where-Object { $rosterChecksSorted -notcontains $_ })
    Assert-Condition -Condition ($rosterChecksExtra.Count -eq 0 -and $rosterChecksMissing.Count -eq 0) -Message (
        "C3k group invariant: Group=checks disagrees with the C3b-A roster || tagged checks but absent " +
        "from roster: " + ($rosterChecksExtra -join ",") + " || in roster but not tagged checks: " +
        ($rosterChecksMissing -join ",") + " || roster(" + $script:TopologyCheckCaseNames.Count + "): " +
        (($script:TopologyCheckCaseNames | Sort-Object) -join ","))

    # C3h-3 加的第 6 条：第 5 条只钉得住 checks 那一档，第三条腿必须有同形的钉，否则
    # "把 union_poly_focus 的 Group 改成 overlay_buffer_join/overlay_ops"改不动四组之并 ⇒ 第 1/2/3/5 条全部照样绿，
    # 而这条腿的真实覆盖已经静默丢失（工单 §4.3 要求的正是这种破坏必须红并点名缺口）。
    # 名单同样不手抄第二份：$script:UnionPolyCaseNames 既是注册名单也是判据分发的门禁。
    $rosterUnionPolyExtra = @($rosterUnionPolySorted | Where-Object { $script:UnionPolyCaseNames -notcontains $_ })
    $rosterUnionPolyMissing = @($script:UnionPolyCaseNames | Where-Object { $rosterUnionPolySorted -notcontains $_ })
    Assert-Condition -Condition ($rosterUnionPolyExtra.Count -eq 0 -and $rosterUnionPolyMissing.Count -eq 0) -Message (
        "C3h-3 group invariant: Group=union_poly disagrees with the C3h-3 roster || tagged union_poly but " +
        "absent from roster: " + ($rosterUnionPolyExtra -join ",") + " || in roster but not tagged union_poly: " +
        ($rosterUnionPolyMissing -join ",") + " || roster(" + $script:UnionPolyCaseNames.Count + "): " +
        (($script:UnionPolyCaseNames | Sort-Object) -join ",") + " || tagged union_poly(" +
        $rosterUnionPolySorted.Count + "): " + ($rosterUnionPolySorted -join ","))

    # C3c-B 加的第 8 条：与第 5/6 条同形，钉住 derive_heavy 这一档的成员名单。
    # 缺它的话"把 simplify_roads 的 Group 改成 overlay_buffer_join"这种改档动不了五组之并
    # （第 1/2/3 条照绿），而这条腿的真实覆盖已经静默丢失 —— 同一件事在 C3h-3 那一档踩过一次。
    $rosterDeriveHeavyExtra = @($rosterDeriveHeavySorted | Where-Object { $script:C3cBCaseNames -notcontains $_ })
    $rosterDeriveHeavyMissing = @($script:C3cBCaseNames | Where-Object { $rosterDeriveHeavySorted -notcontains $_ })
    Assert-Condition -Condition ($rosterDeriveHeavyExtra.Count -eq 0 -and $rosterDeriveHeavyMissing.Count -eq 0) -Message (
        "C3c-B group invariant: Group=derive_heavy disagrees with the C3c-B roster || tagged derive_heavy but " +
        "absent from roster: " + ($rosterDeriveHeavyExtra -join ",") + " || in roster but not tagged derive_heavy: " +
        ($rosterDeriveHeavyMissing -join ",") + " || roster(" + $script:C3cBCaseNames.Count + "): " +
        (($script:C3cBCaseNames | Sort-Object) -join ",") + " || tagged derive_heavy(" +
        $rosterDeriveHeavySorted.Count + "): " + ($rosterDeriveHeavySorted -join ","))
}

# 本档不消费的派生件没有真的造出来 ⇒ 传给判据的 refs 置 $null（而不是"有路径、没现读属性"的
# 半成品对象）：某条腿被归错档却真跑起来时，红在下面那四处同形的 null 判据上，
# 而不是拿 $null 的属性比较蒙成绿。
if (-not $buildOverlayInputs) { $derivedRefs = $null }
if (-not $buildCheckInputs) { $topologyRefs = $null }
if (-not $buildUnionPolyInputs) { $unionPolyRefs = $null }

# 按档过滤：只决定"跑哪些"，不决定"注册哪些"（上面那几份名单要从全量注册里导）。
$runCases = @($cases)
if ($Group -ne "all") {
    $runCases = @($cases | Where-Object { $_.Group -eq $Group })
}
Assert-Condition -Condition ($runCases.Count -gt 0) -Message (
    "C3k: Mode=" + $Mode + " Group=" + $Group + " selected 0 case(s), so nothing would be asserted")

$results = @()
foreach ($case in $runCases) {
    $result = Invoke-Case -ResolvedCliPath $ResolvedCliPath -Case $case
    Validate-CaseOutputs -Case $result -ResolvedCliPath $ResolvedCliPath -ResolvedOutputRoot $ResolvedOutputRoot -InputFeatureCount $InputFeatureCount -DerivedRefs $derivedRefs -TopologyRefs $topologyRefs -ChineseFilterRefs $script:ChineseFilterRefs -UnionPolyRefs $unionPolyRefs -C3cARefs $script:C3cARefs -C3cBRefs $script:C3cBRefs
    $results += $result
}

# C3h-3 第 7 条（评审席 M2 的兜底）：**跑过**的 union_poly 用例数必须等于**被 Assert-UnionPolyCase 判过**的用例数。
# 那条分发按组标走，组标来自 Invoke-Case 带进结果对象的 Group 字段 ⇒ 谁摘掉分发、或谁把 Group 字段从结果对象里
# 弄丢（本刀就是这样静默全绿过一次：整腿 128 s 跑完、零条判据执行、名单四档全部照绿），只有这条会红。
$executedUnionPoly = @($results | Where-Object { $_.Group -eq "union_poly" })
Assert-Condition -Condition ($executedUnionPoly.Count -eq $script:UnionPolyAssertedCount) -Message (
    "C3h-3 judge-coverage invariant: " + $executedUnionPoly.Count + " union_poly case(s) ran but " +
    $script:UnionPolyAssertedCount + " were judged by Assert-UnionPolyCase => the group dispatch is not " +
    "firing, so this leg produced no verdict at all while every roster check still passed")

# C3c-A 同形（按用例名分发）：这四条的名字与 Validate-CaseOutputs 里的四个分支逐字相同。
# 任何一条跑过却没被判（分支被删/名字拼错）都在这里当场红，而不是"整条腿 Passed、零判据"。
$c3cACaseNames = @("info_province", "info_roads", "repair_province", "convert_roads_geojson")
$executedC3cA = @($results | Where-Object { $c3cACaseNames -contains $_.Name })
Assert-Condition -Condition ($executedC3cA.Count -eq $script:C3cAAssertedCount) -Message (
    "C3c-A judge-coverage invariant: " + $executedC3cA.Count + " case(s) ran but " +
    $script:C3cAAssertedCount + " were judged by Assert-C3cA* => the name dispatch is not firing " +
    "(ran: " + (($executedC3cA | ForEach-Object { $_.Name }) -join ",") + ")")

# C3c-B 同形：simplify 那条腿的"跑过数 == 判过数"。名单用 $script:C3cBCaseNames（与注册、组标门禁
# 同一份），名字与 Validate-CaseOutputs 里那个分支逐字相同 ⇒ 分支被删/名字拼错时这里当场红，
# 而不是"整条腿 Passed、零判据"。
$executedC3cB = @($results | Where-Object { $script:C3cBCaseNames -contains $_.Name })
Assert-Condition -Condition ($executedC3cB.Count -eq $script:C3cBAssertedCount) -Message (
    "C3c-B judge-coverage invariant: " + $executedC3cB.Count + " case(s) ran but " +
    $script:C3cBAssertedCount + " were judged by Assert-C3cB* => the name dispatch is not firing " +
    "(ran: " + (($executedC3cB | ForEach-Object { $_.Name }) -join ",") + ")")

# summary_* 的落名：Group=all 时与拆分前逐字相同（summary_quick.* / summary_full.*，
# 那两个文件名是 real_vector_regression / real_vector_regression_full 两个 custom target 的产物）；
# 各分组腿各写自己那一份（summary_full_checks / _overlay_buffer_join / _overlay_ops / _union_poly），免得后跑的档
# 顶着同一个文件名把前一档的用时记录覆盖掉。
$summaryTag = $Mode
if ($Group -ne "all") { $summaryTag = $Mode + "_" + $Group }
$summaryJsonPath = Join-Path $ResolvedOutputRoot ("summary_" + $summaryTag + ".json")
$summaryCsvPath = Join-Path $ResolvedOutputRoot ("summary_" + $summaryTag + ".csv")
Save-RegressionResults -JsonPath $summaryJsonPath -CsvPath $summaryCsvPath -Mode $Mode -Group $Group -Results $results

Write-Host "Vector regression completed:" -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ("  " + $result.Name.PadRight(20) + " " + $result.Seconds + "s")
}
Write-Host ("  summary_json".PadRight(22) + $summaryJsonPath)
Write-Host ("  summary_csv".PadRight(22) + $summaryCsvPath)
