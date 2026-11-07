# regression_case_gate.ps1 -- shared "a case that ran must have been judged" gate.
#
# WHY THIS FILE EXISTS (batch D / R-D33, the bare-switch problem):
#   All three real-data regression runners dispatch their value-level judgements through a
#   `switch ($Case.Name)` that has NO `default` arm, and Invoke-Case only checks the exit code
#   plus "expected output exists". So deleting a judgement branch -- or deleting the New-Case an
#   allowlist entry points at -- keeps the whole suite green while silently removing coverage.
#   This gate makes that visible on every leg, in three directions:
#     forward : every case registered in THIS run must be judged, or be on the allowlist;
#     reverse: every judgement branch declared by THIS runner must belong to a case that
#               exists (registered now, conditionally registered, or in a runtime roster),
#               otherwise it is a decorative branch nobody can reach;
#     allow  : every allowlist entry must still name a case that THIS run registers, or one whose
#               registration site provably exists in the runner source behind a Mode/Group/build
#               gate (the AST-derived ConditionalCase declaration). A name that is simply "not
#               found" is NOT exempted -- that would turn the exception into a pass-on-nothing hole.
#
# CONTRACT WITH THE RUNNERS (four hooks only):
#   Register-Case   -- called from inside each runner's New-Case, so EVERY registration path is
#                      covered, including the vector wrappers that emit `New-Case -Name $Name`
#                      (static literal extraction would miss that whole family).
#   Note-CaseRun    -- called from Invoke-Case: the case really executes on this leg.
#   Mark-Judged     -- called at the top of a branch body that actually executed, never "because
#                      the branch exists".
#   Assert-AllCasesJudged -- called once after the case loop.
#
# HARD-FAILS ON:
#   (1) empty registered roster or empty run scope -- "empty set => everything passes" is the
#       dangerous failure mode of a broken hook, so zero is never a pass;
#   (2) a case that ran but was not judged and is not allowlisted;
#   (3) an allowlist entry that is no longer needed (branch added, entry forgotten);
#   (4) a judgement branch no case can reach (dead branch), unless its registration is derived
#       as conditional (Mode/Group/build-flag gated, read off the AST) or it lives in a runtime
#       roster the runner handed over;
#   (5) on a Group=all leg, a registered case that never reached Invoke-Case;
#   (6) an allowlist entry whose case is neither registered in this run nor backed by a gated
#       registration site in the runner source -- i.e. the exempt case was deleted out from
#       under the entry;
#   (7) judged + exempted-this-run != ran (the counter partition is arithmetic, so a lying
#       hook cannot hide behind a green run);
#   (8) a judged branch whose body carries no derivable value-level expectation while not being
#       named on the no-value roster (N177: "judged=47" used to be readable as "47 value
#       expectations" while six raster branches only compared Size/Type strings);
#   (9) a no-value roster entry whose branch DOES carry a value expectation (stale exemption);
#   (10) a no-value entry naming a case neither judged, nor registered, nor gated-registered;
#        also a blank Reason, or a name listed twice.
#
# THE VALUE SCREEN (checks 8-10) and what it cannot see, stated so nobody reads the split as
# stronger than it is: a branch counts as value-judged only if the AST shows a comparison against a
# numeric literal under an Assert* command, or a call into a helper that does (fixpoint over the
# runner's own functions), or a call into a comparing helper with a numeric literal argument.
#   - a cardinality bound ($rows.Count -ge 2, -eq 42) is deliberately NOT a product value;
#   - a comparison written against another product's number (no literal) is NOT marked;
#   - a vacuous comparison (1 -eq 1) WOULD be marked -- the screen is a screen, not a proof.
# Proof of each arm lives in tests/regression_case_gate_selftest.ps1 (eight arms, run first).
#
# SCOPE STATEMENT (R-D33 item 8): the gate judges THIS run's roster, not the static universe.
# quick/full modes and the Debug lightweight gates register different case sets, and several
# vector groups only register under Mode=full. That is deliberate (it matches the ctest-visible
# scope) -- do not read the printed numbers as "full coverage".
#
# PowerShell 5.1 reads a BOM-less UTF-8 script as ANSI, so every string literal here is ASCII.

if (-not (Get-Variable -Name RegressionGateRegistered -Scope Script -ErrorAction SilentlyContinue)) {
    $script:RegressionGateRegistered = [System.Collections.Generic.List[string]]::new()
    $script:RegressionGateJudged = [System.Collections.Generic.List[string]]::new()
    $script:RegressionGateRunScope = [System.Collections.Generic.List[string]]::new()
}

function Register-Case {
    param([string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) {
        throw "regression gate: Register-Case called with an empty case name"
    }
    if ($script:RegressionGateRegistered.Contains($Name)) {
        throw ("regression gate: case name registered twice: " + $Name +
               " (two cases with one name make the switch dispatch ambiguous)")
    }
    $script:RegressionGateRegistered.Add($Name) | Out-Null
}

function Mark-Judged {
    param([string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) {
        throw "regression gate: Mark-Judged called with an empty case name"
    }
    # A judgement for a name that never registered can only come from a fabricated label.
    # This is also the runtime half of the reverse gate; the AST half is in Get-RegressionGateBranchFacts.
    if (-not $script:RegressionGateRegistered.Contains($Name)) {
        throw ("regression gate: case '" + $Name + "' was judged but never registered in this run")
    }
    if (-not $script:RegressionGateJudged.Contains($Name)) {
        $script:RegressionGateJudged.Add($Name) | Out-Null
    }
}

function Note-CaseRun {
    # Called from Invoke-Case: the case is about to be executed by THIS leg.
    # WHY a third set: the vector runner registers every case of every group but only runs the
    # selected group, so "ran must be judged" has to be measured against the run scope, while the
    # reverse (dead-branch) gate is measured against the constructed roster. Group=all legs get the
    # two compared for equality, so a case cannot be hidden from the gate by filtering it out.
    param([string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) {
        throw "regression gate: Note-CaseRun called with an empty case name"
    }
    if (-not $script:RegressionGateRegistered.Contains($Name)) {
        throw ("regression gate: case '" + $Name + "' is being run but was never registered")
    }
    if (-not $script:RegressionGateRunScope.Contains($Name)) {
        $script:RegressionGateRunScope.Add($Name) | Out-Null
    }
}

function Test-RegressionGateNumericConstant {
    # Windows PowerShell 5.1 hands every numeric literal back as a bare ConstantExpressionAst
    # (there is no Integer/Real subtype in play here -- measured on this box: 12.5 / 1e-3 / 2 all
    # come out as ConstantExpressionAst, while strings are StringConstantExpressionAst). So the
    # test is "constant, not a string, and its value parses as a number in the invariant culture".
    # TryParse is what keeps `3.0.1` (a version) and `$true` out of the numeric set.
    param($Node)
    if ($null -eq $Node) { return $false }
    if (-not ($Node -is [System.Management.Automation.Language.ConstantExpressionAst])) { return $false }
    if ($Node -is [System.Management.Automation.Language.StringConstantExpressionAst]) { return $false }
    $text = [string]$Node.Value
    $d = 0.0
    return [double]::TryParse($text, [System.Globalization.NumberStyles]::Float,
                              [System.Globalization.CultureInfo]::InvariantCulture, [ref]$d)
}

function Test-RegressionGateComparisonOperator {
    # PS spells comparison operators with a case-sensitivity prefix: -lt is TokenKind.Ilt (or Clt),
    # never plain "Lt". Reading only "Lt" marks zero branches -- which is how this list got pinned
    # by a failing self-test rather than by a guess.
    param($OperatorText)
    $ops = @('Ilt', 'Clt', 'Ile', 'Cle', 'Igt', 'Cgt', 'Ige', 'Cge',
             'Ieq', 'Ceq', 'Ine', 'Cne', 'Lt', 'Le', 'Gt', 'Ge', 'Equal', 'Ne')
    return ($ops -contains $OperatorText)
}

function Get-RegressionGateComparisons {
    # Every comparison binary inside $Scope, tagged with the three facts the value screen needs.
    #   Numeric  : one side is a hand-written number -- bare, or arithmetic over numbers, so
    #              `([int]$inherited + 5)` counts (the expectation is written down, not read from
    #              another product).
    #   Countish : the non-literal side is a collection bound ($rows.Count -ge 2). Cardinality is
    #              deliberately NOT a product value -- that exact reading is what N177 was filed
    #              over, and arm A6 pins it.
    #   OperandText: the comparison's own source text, lowercased -- the helper rule uses it to ask
    #              whether the quantity being compared is one of the helper's declared parameters.
    # WHY every caller wraps this in @(): PowerShell unrolls a returned array onto the pipeline, so a
    # scope with exactly ONE comparison comes back as a bare object and `$cmps.Count` on it is $null.
    # A `Count -gt 0` test then reads false for one-comparison helpers and true for two or more --
    # that is how a whole class of helpers looked "does not compare" (arm A10 caught it).
    param($Scope)
    $out = [System.Collections.Generic.List[object]]::new()
    if ($null -eq $Scope) { return @($out) }
    foreach ($bin in $Scope.FindAll(
        { param($n) $n -is [System.Management.Automation.Language.BinaryExpressionAst] }, $true)) {
        if (-not (Test-RegressionGateComparisonOperator ([string]$bin.Operator))) { continue }
        $leftNumeric = Test-RegressionGateNumericArgument $bin.Left
        $rightNumeric = Test-RegressionGateNumericArgument $bin.Right
        # "the other side" is the MEASURED quantity, i.e. whichever side is not the literal.
        # Getting this backwards silently disables the cardinality exclusion, which is exactly what
        # arm A6 exists to catch ($Rows.Count -ge 2 must NOT read as a product value).
        $other = $bin.Left
        if ($leftNumeric -and -not $rightNumeric) { $other = $bin.Right }
        $text = ''
        if ($null -ne $other) { $text = ([string]$other.Extent.Text).TrimEnd() }
        # WHY -cmatch and not -match: `.Count` is the PowerShell collection bound (cardinality), and
        # the legs also read a product field literally named "count" (`$payload.count -le 20`, the
        # max_points cap). A case-insensitive test throws the second one away with the first.
        $countish = ($text -cmatch '\.(Count|Length|LongLength)$')
        $wrapped = $false
        $walker = $bin.Parent
        while ($null -ne $walker) {
            if ($walker -is [System.Management.Automation.Language.CommandAst]) {
                $cn = [string]$walker.GetCommandName()
                if ($cn.StartsWith('Assert')) { $wrapped = $true; break }
            }
            $walker = $walker.Parent
        }
        $out.Add([pscustomobject]@{
            Numeric = ($leftNumeric -or $rightNumeric)
            Countish = $countish
            AssertWrapped = $wrapped
            OperandText = ([string]$bin.Extent.Text).ToLower()
        }) | Out-Null
    }
    return @($out)
}

function Get-RegressionGateParameterNames {
    # Declared parameters of a function, lowercased and without the $.
    # WHY Body.ParamBlock and not FunctionDefinitionAst.Parameters: on 5.1 the latter comes back
    # empty for a `param(...)` function (measured), so reading it silently yields "no parameters"
    # and every parameter-based rule degenerates to false.
    param($FunctionAst)
    $names = [System.Collections.Generic.List[string]]::new()
    if ($null -eq $FunctionAst -or $null -eq $FunctionAst.Body) { return $names }
    $block = $FunctionAst.Body.ParamBlock
    if ($null -eq $block) { return $names }
    foreach ($p in @($block.Parameters)) {
        if ($null -eq $p -or $null -eq $p.Name) { continue }
        $names.Add(([string]$p.Name.VariablePath.UserPath).ToLower()) | Out-Null
    }
    return $names
}

function Test-RegressionGateNumericArgument {
    # Is this argument a hand-written number -- bare, or arithmetic over numbers only?
    # `(10.0 / 255.0)` and `([int]$inherited + 5)` count (the expectation is written out, just not
    # as one token); `("Mean: " + $x)`, `$InputFeatureCount` and `$Rows.Count` do not.
    # WHY member names are skipped: `$Rows.Count` parses as a MemberExpressionAst whose member is a
    # STRING constant, so a naive "any string inside means this is text" test would veto the very
    # cardinality expressions the exclusion is there to catch.
    param($Node)
    if ($null -eq $Node) { return $false }
    if (Test-RegressionGateNumericConstant $Node) { return $true }
    if ($Node -is [System.Management.Automation.Language.StringConstantExpressionAst]) { return $false }
    if ($Node -isnot [System.Management.Automation.Language.ExpressionAst]) { return $false }
    $found = $false
    foreach ($sub in $Node.FindAll(
        { param($n) $n -is [System.Management.Automation.Language.ExpressionAst] }, $true)) {
        if ($sub -is [System.Management.Automation.Language.StringConstantExpressionAst]) {
            # WHY the parent is matched by NAME: on Windows PowerShell 5.1 the static-member node is
            # InvokeMemberExpressionAst and there is no StaticMemberExpressionAst type to test
            # against -- [System.Management.Automation.Language.StaticMemberExpressionAst] throws
            # TypeNotFound at that very line, and a type test that cannot be evaluated is not a test.
            $parentName = ''
            if ($null -ne $sub.Parent) { $parentName = [string]$sub.Parent.GetType().Name }
            if ($parentName -in @('MemberExpressionAst', 'StaticMemberExpressionAst',
                                  'InvokeMemberExpressionAst', 'InvokeExpressionAst')) { continue }
            return $false
        }
        if (Test-RegressionGateNumericConstant $sub) { $found = $true }
    }
    return $found
}

function Test-RegressionGateDecimalText {
    # Does this literal spell out a number with a decimal point or an exponent?
    # WHY that is the text-side test: the legs pin product numbers as whole printed lines
    # ("Mean:   0.75", "Pixel Size:  (0.001, -0.001)", a CSV row "z1,1,0,4,2,1.22474,16,8"), and a
    # decimal is what separates such an expectation from the shape text the same helper is used
    # for ("Size:   24 x 24 x 1 bands", "Type:   Float32", "CRS:    EPSG:4326", "Band 1:"). The
    # separation is not perfect -- "Mean:   0" has no decimal, which is why those branches were
    # moved to -Expected instead of being taught to the screen by a keyword list.
    param([string]$Text)
    if ([string]::IsNullOrEmpty($Text)) { return $false }
    return ($Text -match '\d+\.\d|\.\d+|\d+[eE][+-]?\d')
}

function Get-RegressionGateExpectationArguments {
    # E2/E3 evidence inside one scope: an expected quantity handed to an assertion command, either
    # as a number in an -Expected-shaped parameter or as text that itself carries a decimal.
    # WHY keying on the parameter NAME and not on "any numeric argument": the legs pass four
    # different kinds of number to the same helper -- the expected value (-Expected 0), the array
    # geometry (-ExpectedWidth 32), the precision (-Tolerance 1e-9) and the band index (-Band 1).
    # Only the first is an expectation about the product; arms A11 and A13 pin that.
    param($Scope)
    $commands = @($Scope.FindAll(
        { param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true))
    foreach ($cmd in $commands) {
        $name = ([string]$cmd.GetCommandName()).ToLower()
        if ([string]::IsNullOrWhiteSpace($name)) { continue }
        if (-not $name.StartsWith('assert')) { continue }
        $els = @($cmd.CommandElements)
        $param = ''
        foreach ($el in $els) {
            if ($el -is [System.Management.Automation.Language.CommandParameterAst]) {
                $param = [string]$el.ParameterName
                continue
            }
            if ($el -is [System.Management.Automation.Language.CommandExpressionAst]) {
                $el = $el.Expression
            }
            if ($param -match '^(Expected|Expect|Expectation|ExpectedValue)$') {
                if (Test-RegressionGateNumericArgument $el) { return $true }
            }
            if ($param -notmatch '^(Message|Name|Label|Case)$') {
                if ($el -is [System.Management.Automation.Language.StringConstantExpressionAst]) {
                    if (Test-RegressionGateDecimalText ([string]$el.Value)) { return $true }
                }
            }
        }
    }
    return $false
}

function Get-RegressionGateDecimalComparisonText {
    # E3's other shape: a whole expected line pinned in a comparison operand rather than an
    # argument -- `Assert-Condition -Condition ($lines -contains "z1,1,0,4,2,1.22474,16,8")`, which
    # is how the zonal-stats branch writes its CSV rows. -contains/-match/-like are comparisons on
    # the string level but carry no numeric operand, so the numeric screen (E1) never sees them.
    param($Scope)
    $skip = @('Plus', 'Minus', 'Multiply', 'Divide', 'Mod')
    foreach ($bin in $Scope.FindAll(
        { param($n) $n -is [System.Management.Automation.Language.BinaryExpressionAst] }, $true)) {
        if ($skip -contains ([string]$bin.Operator)) { continue }
        foreach ($side in @($bin.Left, $bin.Right)) {
            if ($side -is [System.Management.Automation.Language.StringConstantExpressionAst]) {
                if (Test-RegressionGateDecimalText ([string]$side.Value)) { return $true }
            }
        }
    }
    return $false
}

function Test-RegressionGateValueScope {
    # Is a value-level expectation written down in this scope? Three shapes, all derived:
    #   E1 a hand-written number compared against the product (not a cardinality bound, and under
    #      an Assert* command -- so "the helper computed a number" is not enough);
    #   E2 an expected quantity passed into an assertion helper as -Expected/-Expect;
    #   E3 an expectation pinned as text that itself carries a decimal.
    # WHY -HelperScope drops E1: inside a helper, "compares a local against a literal" is exactly
    # what a transport check looks like ($code -eq 0, $jsonStart -ge 0), and it says nothing about
    # the product. A helper's own numbers only count when they sit in an E2/E3 expectation shape or
    # in a comparison of the helper's OWN parameters (Test-RegressionGateValueHelper).
    param($Scope, $FunctionMap, $ValueHelpers, [switch]$HelperScope)
    if ($null -eq $Scope) { return $false }
    if (-not $HelperScope) {
        foreach ($c in @(Get-RegressionGateComparisons -Scope $Scope)) {
            if ($c.Numeric -and -not $c.Countish -and $c.AssertWrapped) { return $true }
        }
    }
    if (Get-RegressionGateExpectationArguments -Scope $Scope) { return $true }
    if (Get-RegressionGateDecimalComparisonText -Scope $Scope) { return $true }
    foreach ($cmd in @($Scope.FindAll(
            { param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true))) {
        $name = ([string]$cmd.GetCommandName()).ToLower()
        if ([string]::IsNullOrWhiteSpace($name)) { continue }
        if (-not $FunctionMap.ContainsKey($name)) { continue }
        if ($ValueHelpers.Contains($name)) { return $true }
    }
    return $false
}

function Test-RegressionGateValueHelper {
    # Which helpers may transfer "this branch pins a value" to their callers?
    #   H1 the helper compares one of its OWN declared parameters against a hand-written number --
    #      the expectation flows through the call, which is the point of a value assertion; or
    #   H2 the helper writes an expectation of its own (E2/E3 inside its body).
    # What must NOT qualify: comparing a local the caller never touched. Measured on the raster
    # leg, `Invoke-CliAndCaptureText` (`$code -eq 0`) and `Read-JsonPayload` (`$jsonStart -ge 0`)
    # both passed the old "does it compare anything at all" test, `Get-NamedRasterInfo` then
    # inherited it, and every branch that merely read gdalinfo text was counted as value-checked.
    param($FunctionAst, $OwnParameterNames)
    if ($null -eq $FunctionAst -or $null -eq $OwnParameterNames) { return $false }
    if ($OwnParameterNames.Count -eq 0) { return $false }
    foreach ($cmp in @(Get-RegressionGateComparisons -Scope $FunctionAst.Body)) {
        if (-not $cmp.Numeric -or $cmp.Countish) { continue }
        foreach ($p in $OwnParameterNames) {
            if ([string]::IsNullOrWhiteSpace($p)) { continue }
            if ($cmp.OperandText -match ('\b' + [regex]::Escape($p) + '\b')) { return $true }
        }
    }
    return $false
}


function Get-RegressionGateBranchFacts {
    # Reads the runner's own source with the PowerShell parser (no grep/regex on comment noise).
    # Returns two derived sets:
    #   Branches        -- every literal label of every `switch ($Case.Name)` in the file, i.e. the
    #                      universe of declared judgement branches (the "has a branch" side).
    #   ConditionalCase -- case names whose New-Case/wrapper call site sits inside an `if` that keys
    #                      on Mode / Group / a build-config flag, together with that condition text.
    #                      Derived from source, so it cannot rot the way a hand-copied list does:
    #                      if the gate is removed the name simply stops being exempt.
    param([string]$RunnerPath)

    if (-not (Test-Path $RunnerPath)) {
        throw ("regression gate: runner source not found: " + $RunnerPath)
    }
    $parseTokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile(
        (Resolve-Path $RunnerPath).Path, [ref]$parseTokens, [ref]$parseErrors)
    if ($null -ne $parseErrors -and $parseErrors.Count -gt 0) {
        throw ("regression gate: runner source does not parse (" + $parseErrors.Count +
               " error(s)): " + $RunnerPath)
    }

    $branches = [System.Collections.Generic.List[string]]::new()
    $clauseBodies = @{}
    $switches = $ast.FindAll(
        { param($node) $node -is [System.Management.Automation.Language.SwitchStatementAst] }, $true)
    foreach ($sw in $switches) {
        if ($null -eq $sw.Condition) { continue }
        if (-not ([string]$sw.Condition.Extent.Text).Equals('$Case.Name', [System.StringComparison]::Ordinal)) {
            continue
        }
        foreach ($clause in $sw.Clauses) {
            $labelAst = $clause.Item1
            if ($labelAst -is [System.Management.Automation.Language.StringConstantExpressionAst]) {
                $label = [string]$labelAst.Value
                if (-not $branches.Contains($label)) { $branches.Add($label) | Out-Null }
                if (-not $clauseBodies.ContainsKey($label)) {
                    $clauseBodies[$label] = [System.Collections.Generic.List[object]]::new()
                }
                # A label seen again means two switches judge one name: keep both bodies, and the
                # screen below demands BOTH carry a value expectation (strictness is deliberate --
                # the weaker one is exactly what a reader would not see in a merged count).
                $clauseBodies[$label].Add($clause.Item2) | Out-Null
            }
        }
    }

    $conditional = @{}    # CommandAst only: CommandExpressionAst (also a CommandBaseAst) has no GetCommandName() on PS 5.1.
    $commands = $ast.FindAll(
        { param($node) $node -is [System.Management.Automation.Language.CommandAst] }, $true)
    foreach ($cmd in $commands) {
        if ($null -eq $cmd.CommandElements -or $cmd.CommandElements.Count -lt 2) { continue }
        $commandName = [string]$cmd.GetCommandName()
        if ($commandName -notin @('New-Case', 'New-DerivedCase', 'New-CheckCase')) { continue }
        $caseName = $null
        for ($i = 1; $i -lt $cmd.CommandElements.Count; $i++) {
            $el = $cmd.CommandElements[$i]
            if ($el -is [System.Management.Automation.Language.CommandParameterAst] -and
                [string]$el.ParameterName -eq "Name") {
                if ($i + 1 -lt $cmd.CommandElements.Count) {
                    $valueAst = $cmd.CommandElements[$i + 1]
                    if ($valueAst -is [System.Management.Automation.Language.StringConstantExpressionAst]) {
                        $caseName = [string]$valueAst.Value
                    }
                }
                break
            }
        }
        if ([string]::IsNullOrWhiteSpace($caseName)) { continue }

        $ancestorText = ''
        $walker = $cmd.Parent
        while ($null -ne $walker) {
            if ($walker -is [System.Management.Automation.Language.IfStatementAst]) {
                $condText = [string]$walker.Clauses[0].Item1.Extent.Text
                if ($condText -match '\$Mode' -or $condText -match '\$Group' -or
                    $condText -match 'DebugCliBuild' -or $condText -match 'isDebug') {
                    if ($conditional.ContainsKey($caseName)) {
                        $conditional[$caseName] = $conditional[$caseName] + " || " + $condText
                    } else {
                        $conditional[$caseName] = $condText
                    }
                }
            }
            $walker = $walker.Parent
        }
    }

    $functions = @{}
    $paramNames = @{}
    foreach ($fn in $ast.FindAll(
        { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
        $key = [string]$fn.Name.ToLower()
        $functions[$key] = $fn
        $paramNames[$key] = @(Get-RegressionGateParameterNames -FunctionAst $fn)
    }
    # Fixpoint over the runner's own helpers. A helper is value-bearing when it either writes an
    # expectation itself (E1/E2/E3 inside its body) or compares one of ITS OWN PARAMETERS against a
    # hand-written number (the expectation reaches it through the call), and closure then carries
    # that to anything calling it -- the vector leg drives whole case groups from helper procs.
    # Iterated to stability because the source order of definitions is not a call order.
    # WHY transport helpers stay out of this set: see Test-RegressionGateValueHelper. Measured with
    # them in, 62 of the raster leg's 64 branches read as value-checked, including the four that
    # only ever compare gdalinfo Size/Type text.
    $valueHelpers = [System.Collections.Generic.HashSet[string]]::new()
    $grew = $true
    while ($grew) {
        $grew = $false
        foreach ($name in @($functions.Keys)) {
            if ($valueHelpers.Contains($name)) { continue }
            if ((Test-RegressionGateValueScope -Scope $functions[$name].Body `
                        -FunctionMap $functions -ValueHelpers $valueHelpers -HelperScope) -or
                (Test-RegressionGateValueHelper -FunctionAst $functions[$name] `
                        -OwnParameterNames $paramNames[$name])) {
                $valueHelpers.Add($name) | Out-Null
                $grew = $true
            }
        }
    }

    $valueBranches = [System.Collections.Generic.List[string]]::new()
    foreach ($label in $branches) {
        $marked = $true
        foreach ($body in $clauseBodies[$label]) {
            if (-not (Test-RegressionGateValueScope -Scope $body -FunctionMap $functions `
                        -ValueHelpers $valueHelpers)) { $marked = $false; break }
        }
        if ($marked) { $valueBranches.Add($label) | Out-Null }
    }
    if ($branches.Count -gt 0 -and $valueBranches.Count -eq 0) {
        # A screen that marks nothing is a broken screen, not a suite with no value expectations:
        # passing here would turn every branch into "must be roistered" and let the roster grow
        # until the number means nothing again -- the exact failure N177 is about.
        throw ("regression gate: the value screen marked 0 of " + $branches.Count +
               " judgement branches in " + $RunnerPath + " -- either the assertion shape changed " +
               "(Assert-* renamed, comparisons no longer against literals) or the screen went " +
               "blind. Fix the screen before trusting any judged= number.")
    }

    return [pscustomobject]@{
        Branches        = @($branches)
        ConditionalCase = $conditional
        ValueBranches   = @($valueBranches)
    }
}

function Assert-AllCasesJudged {
    # The closing gate. Hard-fails (throw) on:
    #   (1) an empty registered roster -- the dangerous failure mode of a broken hook is
    #       "empty set => everything passes", so zero is never a pass;
    #   (2) a case that ran but was not judged and is not allowlisted;
    #   (3) an allowlist entry that is no longer needed (someone added the branch and forgot the
    #       entry) -- hand-copied lists rot by keeping stale members, so necessity is asserted;
    #   (4) a judgement branch that no case can reach (dead branch), unless its registration is
    #       derived as conditional or it lives in a runtime roster the runner handed over;
    #   (5) on a Group=all leg, a registered case that never reached Invoke-Case;
    #   (6) an allowlist entry that names no case of this run's roster and has no gated
    #       registration site -- the exemption list may not outlive the case it exempts;
    #   (7) judged + exempted-this-run != ran.
    param(
        [string]$RunnerName,
        [string]$RunnerPath,
        [string]$SelectedGroup = "all",
        [object[]]$Allowlist = @(),
        [object[]]$NoValueList = @(),
        [string[]]$RuntimeRosters = @()
    )

    if ([string]::IsNullOrWhiteSpace($RunnerName) -or [string]::IsNullOrWhiteSpace($RunnerPath)) {
        throw "regression gate: Assert-AllCasesJudged needs both RunnerName and RunnerPath"
    }

    $registered = @($script:RegressionGateRegistered)
    $judged = @($script:RegressionGateJudged)
    $ranCases = @($script:RegressionGateRunScope)
    if ($registered.Count -eq 0) {
        throw ("regression gate[" + $RunnerName + "]: 0 case(s) registered. The registration hook " +
               "or the roster is broken; passing on an empty set would make the gate meaningless.")
    }
    if ($ranCases.Count -eq 0) {
        throw ("regression gate[" + $RunnerName + "]: 0 case(s) reached Invoke-Case, so the run-scope " +
               "hook never fired and 'ran must be judged' would pass on nothing.")
    }

    $facts = Get-RegressionGateBranchFacts -RunnerPath $RunnerPath
    $branches = @($facts.Branches)
    if ($branches.Count -eq 0) {
        throw ("regression gate[" + $RunnerName + "]: 0 judgement branch labels extracted from " +
               $RunnerPath + ". Either the dispatch shape changed or the AST scan is blind; " +
               "the reverse gate must not run against an empty universe.")
    }

    $allowNames = @()
    $allowMissingReason = @()
    foreach ($entry in @($Allowlist)) {
        if ($null -eq $entry) { continue }
        $entryName = [string]$entry.Name
        $entryReason = [string]$entry.Reason
        if ([string]::IsNullOrWhiteSpace($entryReason)) {
            $allowMissingReason += $entryName
        }
        $allowNames += $entryName
    }
    if ($allowMissingReason.Count -gt 0) {
        throw ("regression gate[" + $RunnerName + "]: " + $allowMissingReason.Count +
               " allowlist entry(s) carry no reason: " + ($allowMissingReason -join ",") +
               " -- every entry must state why the case has no judgement.")
    }
    $duplicates = @($allowNames | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    if ($duplicates.Count -gt 0) {
        throw ("regression gate[" + $RunnerName + "]: allowlist lists a name twice: " +
               ($duplicates -join ","))
    }

    # (8) the N177 roster: a branch whose body carries no derivable hand-written value expectation
    #     must be named here, one entry per branch, and the entry must say what the branch does
    #     check. Three lawful kinds of reason, and a reason that does not name one of them is the
    #     gap: (a) shape only -- gdalinfo Size/Type/CRS text, a header column list, a tile grid;
    #     (b) an invariant whose expected side is read at run time (output count == input count), so
    #     no number is written down; (c) a content statistic pinned as a printed INTEGER substring
    #     ("Mean:   0"), which the screen cannot tell from a size line. Kind (c) is a known test
    #     weakness, not a classification, and says so.
    #     WHY a roster instead of a red-by-default rule: forcing fake numeric expectations onto (a)
    #     and (b) would make the assertion worse than the gap. WHY reasons are mandatory: an entry
    #     without one is the "judged=47 read as 47 value expectations" misreading again, just moved
    #     from the counter to the exemption list.
    $noValueNames = @()
    $noValueMissingReason = @()
    foreach ($entry in @($NoValueList)) {
        if ($null -eq $entry) { continue }
        $entryName = [string]$entry.Name
        if ([string]::IsNullOrWhiteSpace([string]$entry.Reason)) {
            $noValueMissingReason += $entryName
        }
        $noValueNames += $entryName
    }
    if ($noValueMissingReason.Count -gt 0) {
        throw ("regression gate[" + $RunnerName + "]: " + $noValueMissingReason.Count +
               " roster entry(ies) carry no reason: " + ($noValueMissingReason -join ",") +
               " -- every entry must state what that branch actually checks (Size/Type strings, " +
               "a column list, a run-time invariant, an integer substring).")
    }
    $noValueDuplicates = @($noValueNames | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    if ($noValueDuplicates.Count -gt 0) {
        throw ("regression gate[" + $RunnerName + "]: the no-value roster lists a name twice: " +
               ($noValueDuplicates -join ","))
    }

    # (2) forward: ran but not judged, and not allowlisted.
    $unjudged = @($ranCases | Where-Object { ($judged -notcontains $_) -and ($allowNames -notcontains $_) })
    # (3) reverse necessity of the allowlist itself.
    $staleAllow = @($allowNames | Where-Object { $judged -contains $_ })
    # (4) reverse: declared branch nobody can reach (measured against the constructed roster).
    $rosterNames = @($RuntimeRosters)
    $reachable = @($registered) + @($facts.ConditionalCase.Keys) + $rosterNames
    $deadBranches = @($branches | Where-Object { $reachable -notcontains $_ })
    # (5) on a Group=all leg the run scope must equal the roster, otherwise a case could be
    #     dropped from $runCases and disappear from the gate's view.
    $notRun = @()
    if ($SelectedGroup -eq "all") {
        $notRun = @($registered | Where-Object { $ranCases -notcontains $_ })
    }
    # (6) MF-1: each allowlist entry must still name a case this run registers. The one lawful way
    #     an entry can point at a case that is missing from THIS run's roster is a declared
    #     conditional registration: Get-RegressionGateBranchFacts found a New-Case /
    #     New-DerivedCase / New-CheckCase call site for exactly that literal name, sitting inside
    #     an if that gates on Mode / Group / a build-config flag. That is positive proof read off
    #     the source, not "lookup failed so let it pass": delete the gated block and the key
    #     disappears, so the entry becomes an orphan and the leg goes red. Without this check,
    #     deleting the New-Case of an EXEMPT case shrinks the roster silently and the leg prints
    #     a green line (measured: registered 67 -> 66, allowlisted unchanged, zero errors).
    $orphanAllow = @($allowNames | Where-Object {
        ($registered -notcontains $_) -and (-not $facts.ConditionalCase.ContainsKey($_)) })
    # (7) MF-2: counter semantics. "allowlisted" means the entries this leg actually consulted to
    #     exempt a case that ran -- not the length of the table, which also counts entries whose
    #     case never registered in this config. Only the first reading makes the partition
    #     judged + allowlisted == ran checkable; the table size is printed next to it, labelled.
    $exemptedThisRun = @($ranCases | Where-Object { $allowNames -contains $_ })
    $exemptNotRegistered = @($allowNames | Where-Object { $registered -notcontains $_ })
    $partitionSum = $judged.Count + $exemptedThisRun.Count

    # (8b) split the judged counter by what the screen derived, so "judged=" can never be read as
    #      a value-coverage number again.
    $valueBranchSet = @($facts.ValueBranches)
    $valueJudged = @($judged | Where-Object { $valueBranchSet -contains $_ })
    $unrosteredNoValue = @($judged | Where-Object {
        ($valueBranchSet -notcontains $_) -and ($noValueNames -notcontains $_) })
    $staleNoValue = @($noValueNames | Where-Object { $valueBranchSet -contains $_ })
    # A roster entry may name a branch this leg did not register: the vector leg runs one group per
    # process, so the other groups' branches are legitimately absent from THIS run. The lawful
    # reference for "ghost" is therefore the file's own branch labels, not this run's roster -- and
    # the necessity check still bites, because deleting the branch deletes the label and the entry
    # then points at nothing.
    $ghostNoValue = @($noValueNames | Where-Object {
        ($judged -notcontains $_) -and ($registered -notcontains $_) -and
        ($branches -notcontains $_) -and (-not $facts.ConditionalCase.ContainsKey($_)) })

    $problems = @()
    if ($unrosteredNoValue.Count -gt 0) {
        $problems += ($unrosteredNoValue.Count.ToString() + " judged branch(es) carry no derivable " +
                     "value-level expectation and are not on the no-value roster: " +
                     ($unrosteredNoValue -join ","))
    }
    if ($staleNoValue.Count -gt 0) {
        $problems += ("roster entry(ies) do carry a value-level expectation, so the entry is " +
                     "stale and must be removed: " + ($staleNoValue -join ","))
    }
    if ($ghostNoValue.Count -gt 0) {
        $problems += ($ghostNoValue.Count.ToString() + " roster entry(ies) name a case that was " +
                     "neither judged, nor registered, nor declared as a judgement branch of " +
                     $RunnerPath + ", nor conditionally registered -- the branch or the case is " +
                     "gone: " + ($ghostNoValue -join ","))
    }
    if ($unjudged.Count -gt 0) {
        $problems += ($unjudged.Count.ToString() + " case(s) ran with no judgement branch and no " +
                     "allowlist entry: " + ($unjudged -join ","))
    }
    if ($staleAllow.Count -gt 0) {
        $problems += ("allowlist entry(ies) are now judged, so the entry is stale and must be " +
                     "removed: " + ($staleAllow -join ","))
    }
    if ($deadBranches.Count -gt 0) {
        $problems += ($deadBranches.Count.ToString() + " judgement branch(es) no case can reach " +
                     "(not registered, not conditionally registered, not in a runtime roster): " +
                     ($deadBranches -join ","))
    }
    if ($notRun.Count -gt 0) {
        $problems += ($notRun.Count.ToString() + " registered case(s) never reached Invoke-Case on a " +
                     "Group=all leg: " + ($notRun -join ","))
    }
    if ($orphanAllow.Count -gt 0) {
        $problems += ($orphanAllow.Count.ToString() + " allowlist entry(ies) name no case of this " +
                     "run's roster and have no gated registration site in " + $RunnerPath + " -- the " +
                     "exempt case was deleted (or moved out of its Mode/Group/build gate), so the " +
                     "entry is a ghost and must be deleted with it: " + ($orphanAllow -join ","))
    }
    if ($partitionSum -ne $ranCases.Count) {
        $problems += ("counter partition is broken: judged=" + $judged.Count + " + allowlisted(this " +
                     "run, ran-but-not-judged)=" + $exemptedThisRun.Count + " = " + $partitionSum +
                     " but ran=" + $ranCases.Count + "; a judgement or run hook is not accounting " +
                     "for every case that ran")
    }
    if ($problems.Count -gt 0) {
        throw ("regression gate[" + $RunnerName + "] FAILED -- " + ($problems -join " || ") +
               " || scope: this run only (Mode-registered roster), not the static universe || " +
               "registered=" + $registered.Count + " ran=" + $ranCases.Count +
               " judged=" + $judged.Count + " allowlisted=" + $exemptedThisRun.Count +
               " allowlist-table=" + $allowNames.Count +
               " allowlist-gated-not-registered=" + $exemptNotRegistered.Count +
               " branch-labels=" + $branches.Count +
               " value-judged=" + $valueJudged.Count + " no-value-judged=" + ($judged.Count - $valueJudged.Count) +
               " no-value-roster=" + $noValueNames.Count)
    }

    # The counters are returned as well as printed, from the SAME variables: a self-test must be
    # able to assert on the contract instead of scraping console text (Write-Host output cannot be
    # captured from a script block in 5.1, and scraping it would also couple the proof to layout).
    $summary = [pscustomobject]@{
        Registered   = $registered.Count
        Ran          = $ranCases.Count
        Judged       = $judged.Count
        ValueJudged  = $valueJudged.Count
        NoValueJudged  = ($judged.Count - $valueJudged.Count)
        NoValueRoster  = $noValueNames.Count
        Allowlisted  = $exemptedThisRun.Count
        AllowlistTbl = $allowNames.Count
        BranchLabels = $branches.Count
    }

    # One line, printed only when the gate actually passed.
    # allowlisted = entries this leg consulted for a case that ran and has no branch (so
    # judged + allowlisted == ran here); allowlist-table = the list handed over by the runner;
    # the difference between those two is exactly the entries whose case is gated out of this
    # config and is only lawful while the gated registration site still exists (check (6)).
    # value-judged / no-value-judged split the judged counter by the AST screen (N177): only
    # value-judged may be read as "the leg compares product numbers against a written-down
    # expectation"; the no-value side is covered by a reason in the roster, never by silence.
    Write-Host ("regression-gate[" + $RunnerName + "]: registered=" + $summary.Registered +
                " ran=" + $summary.Ran + " judged=" + $summary.Judged +
                " value-judged=" + $summary.ValueJudged +
                " no-value-judged=" + $summary.NoValueJudged +
                " no-value-roster=" + $summary.NoValueRoster +
                " allowlisted=" + $summary.Allowlisted +
                " allowlist-table=" + $summary.AllowlistTbl +
                " allowlist-gated-not-registered=" + $exemptNotRegistered.Count +
                " branch-labels=" + $summary.BranchLabels +
                " conditional-registrations=" + $facts.ConditionalCase.Keys.Count +
                " dead-branches=0 (scope: this run only, Group=" + $SelectedGroup + ")")
    return $summary
}
