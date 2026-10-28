# regression_case_gate.ps1 -- shared "a case that ran must have been judged" gate.
#
# WHY THIS FILE EXISTS (batch D / R-D33, the bare-switch problem):
#   All three real-data regression runners dispatch their value-level judgements through a
#   `switch ($Case.Name)` that has NO `default` arm, and Invoke-Case only checks the exit code
#   plus "expected output exists". So deleting a judgement branch -- or deleting a New-Case --
#   keeps the whole suite green while silently removing coverage. This gate makes that visible
#   on every leg, in both directions:
#     forward : every case registered in THIS run must be judged, or be on the allowlist;
#     reverse: every judgement branch declared by THIS runner must belong to a case that
#               exists (registered now, conditionally registered, or in a runtime roster),
#               otherwise it is a decorative branch nobody can reach.
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
#   (5) on a Group=all leg, a registered case that never reached Invoke-Case.
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
            }
        }
    }

    $conditional = @{}
    # CommandAst only: CommandExpressionAst (also a CommandBaseAst) has no GetCommandName() on PS 5.1.
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

    return [pscustomobject]@{
        Branches        = @($branches)
        ConditionalCase = $conditional
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
    #       derived as conditional or it lives in a runtime roster the runner handed over.
    param(
        [string]$RunnerName,
        [string]$RunnerPath,
        [string]$SelectedGroup = "all",
        [object[]]$Allowlist = @(),
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

    $problems = @()
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
    if ($problems.Count -gt 0) {
        throw ("regression gate[" + $RunnerName + "] FAILED -- " + ($problems -join " || ") +
               " || scope: this run only (Mode-registered roster), not the static universe || " +
               "registered=" + $registered.Count + " ran=" + $ranCases.Count +
               " judged=" + $judged.Count + " allowlisted=" + $allowNames.Count +
               " branch-labels=" + $branches.Count)
    }

    # One line, printed only when the gate actually passed.
    Write-Host ("regression-gate[" + $RunnerName + "]: registered=" + $registered.Count +
                " ran=" + $ranCases.Count + " judged=" + $judged.Count +
                " allowlisted=" + $allowNames.Count + " branch-labels=" + $branches.Count +
                " conditional-registrations=" + $facts.ConditionalCase.Keys.Count +
                " dead-branches=0 (scope: this run only, Group=" + $SelectedGroup + ")")
}
