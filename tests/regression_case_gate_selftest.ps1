# regression_case_gate_selftest.ps1 -- the gate's own proof, against a synthetic fixture runner.
#
# WHY THIS EXISTS (batch D / N177): the gate used to count "Mark-Judged" as one number, and that
# number was being read as "N value-level expectations" while a branch only had to *run* its body.
# Measured on the three legs once the screen actually derived the split: 43 of 64 raster branches,
# 28 of 48 vector and 3 of 5 matching carry a hand-written expectation, and the other 42 are on the
# legs' no-value rosters with a reason each. Two earlier attempts at this classifier were both wrong
# -- the hand-labelled one called two genuinely value-checked branches "struct-only", and the first
# AST one marked 62 of 64 because it followed any call into any helper that compares anything
# (the CLI transport helper's `$code -eq 0` counted as a product value). So the screen is AST-derived
# AND the screen itself has to be able to go red -- that is this file. Each arm is a fixture branch
# whose body is the shape under test; none of them can be satisfied by editing the real legs.
#
# ARMS (each names the production change that must make it fail):
#   A1 green  : value_direct + value_helper judged, unmarked ones rostered with reasons -> passes.
#   A2 red    : a branch whose body has no value expectation, judged but NOT rostered -> throw.
#   A3 red    : a genuinely value-checked branch listed as unmarked -> stale entry -> throw.
#   A4 red    : an unmarked entry with a blank Reason -> throw.
#   A5 red    : an unmarked entry naming a case that is not judged and has no gated registration -> throw.
#   A6 screen : "$rows.Count -ge 2" is a cardinality bound, NOT a value expectation -> must be unmarked
#               (this is the exact misreading N177 was filed over, so the screen is pinned on it).
#   A7 screen : a branch whose only check is a call into a helper that compares numbers against a
#               numeric call-site argument must count as value-marked (closure through helpers).
#   A8 screen : Assert-TextContains against "Size: ... bands" / "Type: Float32" alone is NOT value.
#   A9 screen : a helper that only compares its OWN local (the CLI exit code) is transport and must
#               not transfer a value expectation to its caller.
#   A10 screen: an integer expectation handed to -Expected IS value (no decimal needed).
#   A11 screen: array geometry (-ExpectedWidth/-ExpectedHeight/-ExpectedBands) is NOT value.
#   A12 screen: a whole CSV row pinned in a -contains IS value even though no operand is numeric.
#   A13 screen: `-Expected $Rows.Count` with `-Tolerance 1e-9` pins no hand-written number.
#   A14 screen: `$Stat.count -le 20` is a product field named "count", not the collection bound, so
#               the cardinality exclusion (case-sensitive) must not swallow it.
#   A15 screen: a hand-written number nested in an operand (`([int]$Rows.Count + 5)`) still counts.
#
# PowerShell 5.1 reads a BOM-less UTF-8 script as ANSI, so every string literal here is ASCII.

param(
    [string]$GatePath = ""
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($GatePath)) {
    $GatePath = Join-Path $PSScriptRoot 'regression_case_gate.ps1'
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('rGateSelftest_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null

# The fixture is a runner-shaped file: Register-Case / switch ($Case.Name) / helpers. It is never
# executed -- the gate reads it with the PowerShell parser, exactly like it reads the real runners.
$fixtureLines = @(
    'function Register-Case { param([string]$Name) }'
    'function Assert-Condition {'
    '    param([bool]$Condition, [string]$Message)'
    '    if (-not $Condition) { throw $Message }'
    '}'
    'function Assert-TextContains {'
    '    param([string]$Text, [string[]]$Needles)'
    '    foreach ($n in $Needles) { if (-not $Text.Contains($n)) { throw "missing" } }'
    '}'
    'function Assert-SizeAndType {'
    '    param([string]$Info)'
    '    Assert-TextContains -Text $Info -Needles @("Size")'
    '}'
    'function Assert-NearValue {'
    '    param($Actual, $Expect, $Tol)'
    '    Assert-Condition -Condition (((($Actual - $Expect) -lt $Tol) -and (($Expect - $Actual) -lt $Tol)) -or $false) -Message "near"'
    '}'
    # A transport helper: it does compare a number, but the number is its OWN local (the exit code),
    # never a parameter, and never the product. Following such a helper is what made 62 of 64 raster
    # branches read as value-checked (arms A9/A13 pin the fix).
    'function Invoke-CliLike {'
    '    param([string[]]$Arguments)'
    '    $code = 0'
    '    Assert-Condition -Condition ($code -eq 0) -Message "cli failed"'
    '    return "text"'
    '}'
    # The legs' own convention (see the D1-T1 header comment in run_real_raster_regression.ps1): a
    # hand-written expected quantity is passed as -Expected/-Expect; precision goes in -Tolerance.
    'function Assert-InfoStatNear {'
    '    param([string]$Text, [string]$Label, [double]$Expected, [double]$Tolerance, [string]$Message)'
    '    Assert-Condition -Condition ($Expected -lt ($Expected + $Tolerance)) -Message $Message'
    '}'
    # Array geometry, deliberately NOT named -Expected: width/height/band count are shape.
    'function Assert-InfoSizeExact {'
    '    param([string]$Text, [int]$ExpectedWidth, [int]$ExpectedHeight, [int]$ExpectedBands, [string]$Message)'
    '    Assert-Condition -Condition ($ExpectedWidth -eq $ExpectedHeight) -Message $Message'
    '}'
    'function Invoke-Case { param($Case) }'
    'function Validate-CaseOutputs {'
    '    param($Case, [string]$Info, $Rows, $Stat)'
    '    switch ($Case.Name) {'
    '        value_direct {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-Condition -Condition (($Stat.Mean - 12.5) -lt 1e-3) -Message "mean"'
    '        }'
    '        value_helper {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-NearValue -Actual $Stat.Mean -Expect 3.0 -Tol 1e-6'
    '        }'
    '        shape_text {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-SizeAndType -Info $Info'
    '        }'
    '        shape_count {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-Condition -Condition ($Rows.Count -ge 2) -Message "rows"'
    '        }'
    '        count_exact {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-Condition -Condition ($Rows.Count -eq 42) -Message "rows exact"'
    '        }'
    '        transport_helper {'
    '            Mark-Judged -Name $Case.Name'
    '            $t = Invoke-CliLike -Arguments @("raster_inspect", "info")'
    '            Assert-TextContains -Text $t -Expected "Size: 4 x 4" -Message "size text"'
    '        }'
    '        expect_int_stat {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-InfoStatNear -Text $Info -Label "Mean" -Expected 0 -Tolerance 1e-9 -Message "mean"'
    '        }'
    '        expect_size_only {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-InfoSizeExact -Text $Info -ExpectedWidth 32 -ExpectedHeight 32 -ExpectedBands 3 -Message "size"'
    '        }'
    '        expect_csv_row {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-Condition -Condition ($Info -contains "z1,1,0,4,2,1.22474,16,8") -Message "csv row"'
    '        }'
    '        invariant_count {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-InfoStatNear -Text $Info -Label "Mean" -Expected $Rows.Count -Tolerance 1e-9 -Message "inv"'
    '        }'
    '        field_count_cap {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-Condition -Condition ($Stat.count -le 20) -Message "cap"'
    '        }'
    '        derived_offset {'
    '            Mark-Judged -Name $Case.Name'
    '            Assert-Condition -Condition ([int]$Stat.Fields -eq ([int]$Rows.Count + 5)) -Message "offset"'
    '        }'
    '    }'
    '}'
)
$fixturePath = Join-Path $tmp 'fixture_runner.ps1'
Set-Content -Path $fixturePath -Value $fixtureLines -Encoding ASCII

. $GatePath

$results = [System.Collections.Generic.List[string]]::new()
$failed = 0

# Capability probe, so a missing parameter shows up as a FAILED ARM with the real reason instead of
# a parameter-binding error that the "expect throw" arms would happily count as a pass.
$gateCmd = Get-Command Assert-AllCasesJudged
$hasRosterParam = [bool]($gateCmd.Parameters.Keys -contains 'NoValueList')

function Invoke-Arm {
    param([string]$Name, [scriptblock]$Body, [bool]$ExpectThrow,
          [hashtable]$ExpectFields = @{}, [string]$ExpectMessage = "")

    if (-not $hasRosterParam) {
        $script:Results.Add("FAIL  $Name  [gate has no -NoValueList parameter yet: N177 unimplemented]")
        $script:failed++
        return
    }
    $threw = $false
    $msg = ""
    $ret = $null
    try {
        $ret = & $Body
    } catch {
        $threw = $true
        $msg = $_.Exception.Message
    }
    $ok = ($threw -eq $ExpectThrow)
    if ($ok -and $ExpectThrow -and $ExpectMessage -ne "") {
        $ok = $msg.Contains($ExpectMessage)
    }
    if ($ok -and -not $ExpectThrow) {
        if ($null -eq $ret) {
            # The gate passed but returned nothing: the arms could only assert on console text,
            # which Write-Host does not deliver to a script block in 5.1. Contract = an object.
            $ok = $false
            $msg = "gate returned no summary object"
        } else {
            foreach ($k in $ExpectFields.Keys) {
                if ([int]$ret.$k -ne [int]$ExpectFields[$k]) {
                    $ok = $false
                    $msg = ("field " + $k + " expected " + $ExpectFields[$k] + " got " + $ret.$k)
                }
            }
        }
    }
    $script:Results.Add(("{0}  {1}{2}" -f $(if ($ok) { "OK  " } else { "FAIL" }), $Name,
                         $(if ($ok) { "" } else { "  [threw=" + $threw + " msg=" + $msg + "]" })))
    if (-not $ok) { $script:failed++ }
}

function New-RosterEntry {
    param([string]$Name, [string]$Reason)
    return [pscustomobject]@{ Name = $Name; Reason = $Reason }
}

function Reset-Hooks {
    param([string[]]$Registered, [string[]]$Ran, [string[]]$Judged)
    $script:RegressionGateRegistered = [System.Collections.Generic.List[string]]::new()
    $script:RegressionGateJudged = [System.Collections.Generic.List[string]]::new()
    $script:RegressionGateRunScope = [System.Collections.Generic.List[string]]::new()
    foreach ($n in $Registered) { $script:RegressionGateRegistered.Add($n) | Out-Null }
    foreach ($n in $Ran) { $script:RegressionGateRunScope.Add($n) | Out-Null }
    foreach ($n in $Judged) { $script:RegressionGateJudged.Add($n) | Out-Null }
}

$allCases = @('value_direct', 'value_helper', 'shape_text', 'shape_count', 'count_exact',
              'transport_helper', 'expect_int_stat', 'expect_size_only', 'expect_csv_row',
              'invariant_count', 'field_count_cap', 'derived_offset')

$goodRoster = @(
    (New-RosterEntry -Name 'shape_text' -Reason 'checks the gdalinfo Size/Type lines only')
    (New-RosterEntry -Name 'shape_count' -Reason 'a row-count lower bound, not a product value')
    (New-RosterEntry -Name 'count_exact' -Reason 'an exact record count, cardinality not value content')
    (New-RosterEntry -Name 'transport_helper' -Reason 'calls the CLI transport helper and matches Size text')
    (New-RosterEntry -Name 'expect_size_only' -Reason 'array geometry (-ExpectedWidth/-Height/-Bands), not content')
    (New-RosterEntry -Name 'invariant_count' -Reason 'invariant against a runtime count, no hand-written number')
)

# A1: everything either value-marked or rostered -> the gate must pass, and the printed line must
#     carry the two separate counters (that split is the point of N177).
Invoke-Arm -Name 'A1 green: value + rostered shape passes' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran $allCases -Judged $allCases
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'all' `
        -Allowlist @() -NoValueList $goodRoster
} -ExpectFields @{ValueJudged = 6; NoValueJudged = 6; Judged = 12}

# A2: an unmarked branch with no roster entry -> throw naming the case.
Invoke-Arm -Name 'A2 red: unmarked branch without a roster entry' -ExpectThrow $true -Body {
    Reset-Hooks -Registered $allCases -Ran $allCases -Judged $allCases
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'all' `
        -Allowlist @() -NoValueList @(
            (New-RosterEntry -Name 'shape_text' -Reason 'x'),
            (New-RosterEntry -Name 'count_exact' -Reason 'x'))
} -ExpectMessage 'shape_count'

# A3: roistering a genuinely value-checked branch is a stale exemption.
Invoke-Arm -Name 'A3 red: value-marked branch must not sit on the roster' -ExpectThrow $true -Body {
    Reset-Hooks -Registered $allCases -Ran $allCases -Judged $allCases
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'all' `
        -Allowlist @() -NoValueList (@($goodRoster) + @(
            (New-RosterEntry -Name 'value_direct' -Reason 'wrongly exempted')))
} -ExpectMessage 'value_direct'

# A4: an exemption without a reason.
Invoke-Arm -Name 'A4 red: roster entry with a blank reason' -ExpectThrow $true -Body {
    Reset-Hooks -Registered $allCases -Ran $allCases -Judged $allCases
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'all' `
        -Allowlist @() -NoValueList @(
            (New-RosterEntry -Name 'shape_text' -Reason ''),
            (New-RosterEntry -Name 'shape_count' -Reason 'x'),
            (New-RosterEntry -Name 'count_exact' -Reason 'x'))
} -ExpectMessage 'reason'

# A5: a roster entry for a case that this run never judged and that is not gated-registered.
Invoke-Arm -Name 'A5 red: roster entry with no judged case behind it' -ExpectThrow $true -Body {
    Reset-Hooks -Registered $allCases -Ran $allCases -Judged $allCases
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'all' `
        -Allowlist @() -NoValueList (@($goodRoster) + @(
            (New-RosterEntry -Name 'ghost_case' -Reason 'not in this run at all')))
} -ExpectMessage 'ghost_case'

# A6: the screen must treat "$rows.Count -ge 2" as cardinality, not a product value. If the screen
#     ever starts marking it, the roster entry becomes stale and A3-style necessity fires: prove the
#     classification directly instead of relying on the exemption surviving.
#     WHY all five cases are still registered: the fixture declares five branches, and the
#     pre-existing dead-branch check would fire for the four not under test. Registering them and
#     selecting a non-'all' group is what a real single-group leg does, so the arm measures only
#     the screen.
Invoke-Arm -Name 'A6 screen: count bound is NOT value-marked' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('shape_count') -Judged @('shape_count')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @(
            (New-RosterEntry -Name 'shape_count' -Reason 'row count bound'))
} -ExpectFields @{ValueJudged = 0; NoValueJudged = 1}

# A7: the screen must follow a call into a helper that compares numbers against numeric arguments.
Invoke-Arm -Name 'A7 screen: value helper call marks the branch' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('value_helper') -Judged @('value_helper')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @()
} -ExpectFields @{ValueJudged = 1; NoValueJudged = 0}

# A8: Size/Type text checks alone must not be marked.
Invoke-Arm -Name 'A8 screen: Size/Type text is NOT value-marked' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('shape_text') -Judged @('shape_text')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @(
            (New-RosterEntry -Name 'shape_text' -Reason 'gdalinfo Size/Type only'))
} -ExpectFields @{ValueJudged = 0; NoValueJudged = 1}

# A9: a helper whose only comparison is against its OWN local (the CLI exit code) is transport, not
#     a product value. Before the fix this arm goes RED through the stale-entry branch: the screen
#     marks the caller via helper transitivity, so the roster entry is refused.
Invoke-Arm -Name 'A9 screen: transport helper does not transfer value' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('transport_helper') -Judged @('transport_helper')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @(
            (New-RosterEntry -Name 'transport_helper' -Reason 'CLI transport helper + Size text only'))
} -ExpectFields @{ValueJudged = 0; NoValueJudged = 1}

# A10: the legs write their expected statistic as `-Expected 0` on a numeric-typed parameter, so an
#      INTEGER expectation must be visible without a decimal point. This arm is green before the
#      change too (the old weak helper rule caught it by accident); it pins that tightening the
#      helper rule must not lose it.
Invoke-Arm -Name 'A10 screen: -Expected integer marks the branch' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('expect_int_stat') -Judged @('expect_int_stat')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @()
} -ExpectFields @{ValueJudged = 1; NoValueJudged = 0}

# A11: array geometry is NOT content. -ExpectedWidth/-ExpectedHeight/-ExpectedBands are numbers, but
#     named as geometry, and the branch asserts nothing else. RED before: the old rule saw "helper
#     compares + call site has a numeric literal" and marked it.
Invoke-Arm -Name 'A11 screen: geometry params are not value expectations' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('expect_size_only') -Judged @('expect_size_only')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @(
            (New-RosterEntry -Name 'expect_size_only' -Reason 'width/height/bands only'))
} -ExpectFields @{ValueJudged = 0; NoValueJudged = 1}

# A12: a product row pinned as a whole CSV line IS a value expectation even though no comparison
#     operator has a numeric operand. RED before: -contains is not a comparison operator, so the
#     branch was unmarked and the empty roster made the leg throw.
Invoke-Arm -Name 'A12 screen: decimal inside a pinned text expectation marks' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('expect_csv_row') -Judged @('expect_csv_row')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @()
} -ExpectFields @{ValueJudged = 1; NoValueJudged = 0}

# A13: an invariant whose expected side is read at run time (`-Expected $Rows.Count`) pins no
#     hand-written number -- and the -Tolerance 1e-9 next to it must not be mistaken for one.
#     RED before: the old rule keyed on "any numeric literal in the call".
Invoke-Arm -Name 'A13 screen: runtime invariant with -Tolerance is not value' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('invariant_count') -Judged @('invariant_count')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @(
            (New-RosterEntry -Name 'invariant_count' -Reason 'expected count read from the input'))
} -ExpectFields @{ValueJudged = 0; NoValueJudged = 1}

# A14: the legs read a JSON field literally named "count" (`$payload.count -le 20` is the
#      max_points cap). A case-insensitive cardinality test throws that away with `$rows.Count`,
#      and the matching leg loses its only hand-written bound.
Invoke-Arm -Name 'A14 screen: product field count is not the collection bound' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('field_count_cap') -Judged @('field_count_cap')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @()
} -ExpectFields @{ValueJudged = 1; NoValueJudged = 0}

# A15: the number can sit inside an operand instead of next to it -- `([int]$inherited + 5)` states
#      the five metric fields the action must add, and a screen that only looks at the operand's own
#      type never sees it.
Invoke-Arm -Name 'A15 screen: number nested in an operand marks' -ExpectThrow $false -Body {
    Reset-Hooks -Registered $allCases -Ran @('derived_offset') -Judged @('derived_offset')
    Assert-AllCasesJudged -RunnerName 'fixture' -RunnerPath $fixturePath -SelectedGroup 'group1' `
        -Allowlist @() -NoValueList @()
} -ExpectFields @{ValueJudged = 1; NoValueJudged = 0}

foreach ($line in $results) { Write-Host $line }
Write-Host ("regression-gate-selftest: arms=" + $results.Count + " failed=" + $failed)
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($failed -gt 0) { exit 1 }
exit 0
