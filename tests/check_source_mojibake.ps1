# tests/check_source_mojibake.ps1
# Guard: source text must not contain mojibake (GBK/UTF-8 double-encoding residue).
# Four rules (see docs/RELEASE.md, B17):
#   R1 ROUNDTRIP  : run encoded as CP936 decodes as valid UTF-8 into different CJK text
#   R2 PUA        : U+E000-U+F8FF (private use area)
#   R3 BOPOMOFO   : U+3105-U+312F (Zhuyin never appears in this project's UI copy)
#   R4 ODDFORMS   : U+FE30-U+FE4F presentation forms, U+FFE0-U+FFE6 fullwidth currency,
#                   U+20AC euro, U+FFFD replacement char
# Output is ASCII-only on purpose: Windows PowerShell 5.1 renders non-ASCII stdout as garbage,
# so a Chinese message here would be unreadable in exactly the failure case.
# Exit 0 = clean, 1 = violations or self-test failure.

param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$FixtureDir = ""
)

$ErrorActionPreference = 'Stop'
$ascii = New-Object System.Text.ASCIIEncoding

$strict936 = [System.Text.Encoding]::GetEncoding(
    936,
    (New-Object System.Text.EncoderExceptionFallback),
    (New-Object System.Text.DecoderExceptionFallback))
$utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)

function Get-Runs([string]$s) {
    # maximal runs of non-ASCII chars, length >= 2
    $out = New-Object System.Collections.ArrayList
    $cur = ''
    foreach ($ch in $s.ToCharArray()) {
        if ([int]$ch -gt 127) { $cur += $ch }
        else {
            if ($cur.Length -ge 2) { [void]$out.Add($cur) }
            $cur = ''
        }
    }
    if ($cur.Length -ge 2) { [void]$out.Add($cur) }
    return $out
}

function Test-Roundtrip([string]$run) {
    try {
        $bytes = $strict936.GetBytes($run)
    } catch { return $false }
    try { $rt = $utf8Strict.GetString($bytes) } catch { return $false }
    if ($rt -eq $run) { return $false }
    foreach ($ch in $rt.ToCharArray()) {
        $c = [int]$ch
        if ($c -ge 0x4E00 -and $c -le 0x9FFF) { return $true }
    }
    return $false
}

function Find-Rules([string]$line) {
    $hits = New-Object System.Collections.ArrayList
    foreach ($run in (Get-Runs $line)) {
        foreach ($ch in $run.ToCharArray()) {
            $c = [int]$ch
            if ($c -ge 0xE000 -and $c -le 0xF8FF) { [void]$hits.Add('R2_PUA') }
            elseif ($c -ge 0x3105 -and $c -le 0x312F) { [void]$hits.Add('R3_BOPOMOFO') }
            elseif (($c -ge 0xFE30 -and $c -le 0xFE4F) -or ($c -ge 0xFFE0 -and $c -le 0xFFE6) `
                    -or $c -eq 0x20AC -or $c -eq 0xFFFD) { [void]$hits.Add('R4_ODDFORMS') }
        }
        if (Test-Roundtrip $run) { [void]$hits.Add('R1_ROUNDTRIP') }
    }
    return ($hits | Sort-Object -Unique)
}

function Read-Utf8([string]$path) {
    return [System.IO.File]::ReadAllText($path, [System.Text.UTF8Encoding]::new($false))
}

# ---- self test: the detector itself must not be vacuous -------------------------------
if ($FixtureDir -eq "") { $FixtureDir = Join-Path $Root 'tests/mojibake_fixtures' }
$badPath = Join-Path $FixtureDir 'mojibake_bad.txt'
$okPath = Join-Path $FixtureDir 'mojibake_ok.txt'
if (-not (Test-Path $badPath)) { Write-Output "SELFTEST FAIL: missing fixture $badPath"; exit 1 }
if (-not (Test-Path $okPath)) { Write-Output "SELFTEST FAIL: missing fixture $okPath"; exit 1 }

$badRules = New-Object System.Collections.ArrayList
foreach ($bl in (Read-Utf8 $badPath).Split("`n")) {
    foreach ($r in (Find-Rules $bl)) { [void]$badRules.Add($r) }
}
$badRules = ($badRules | Sort-Object -Unique)
$okHits = New-Object System.Collections.ArrayList
foreach ($ol in (Read-Utf8 $okPath).Split("`n")) {
    foreach ($r in (Find-Rules $ol)) { [void]$okHits.Add($r) }
}
Write-Output ("SELFTEST bad-fixture rules: " + ($badRules -join ','))
if ($badRules.Count -lt 4) {
    Write-Output ("SELFTEST FAIL: bad fixture must trigger all 4 rules, got " + $badRules.Count)
    exit 1
}
if (@($okHits).Count -gt 0) {
    Write-Output ("SELFTEST FAIL: clean fixture flagged as mojibake: " + ((@($okHits) | Sort-Object -Unique) -join ','))
    exit 1
}

# ---- repo scan ------------------------------------------------------------------------
$exts = @('*.cpp', '*.h', '*.hpp', '*.cc', '*.inc', '*.cmake', '*.txt', '*.json', '*.nsi', '*.ps1')
$targets = New-Object System.Collections.ArrayList
foreach ($dir in @('src', 'tests', 'cmake', '.github')) {
    $d = Join-Path $Root $dir
    if (-not (Test-Path $d)) { continue }
    foreach ($e in $exts) {
        foreach ($f in (Get-ChildItem -Path $d -Recurse -Filter $e -File)) {
            if ($f.FullName -like ('*' + [IO.Path]::DirectorySeparatorChar + 'mojibake_fixtures' + '*')) { continue }
            [void]$targets.Add($f.FullName)
        }
    }
}
foreach ($rf in @('CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json')) {
    $p = Join-Path $Root $rf
    if (Test-Path $p) { [void]$targets.Add($p) }
}

$violations = 0
foreach ($t in ($targets | Sort-Object -Unique)) {
    $text = Read-Utf8 $t
    $n = 0
    foreach ($line in $text.Split("`n")) {
        $n++
        $rs = Find-Rules $line
        if (@($rs).Count -gt 0) {
            $violations++
            $rel = $t.Substring($Root.Length).TrimStart('\', '/')
            Write-Output ("MOJIBAKE " + ($rs -join '+') + " " + $rel + ":" + $n)
        }
    }
}
Write-Output ("scanned files=" + (@($targets | Sort-Object -Unique).Count) + " violations=" + $violations)
if ($violations -gt 0) { exit 1 }
exit 0
