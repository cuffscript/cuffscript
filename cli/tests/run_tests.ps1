<#
.SYNOPSIS
  Tests cuffsh on Windows (and, with PowerShell 7, on Linux/macOS). Works in Windows PowerShell 5.1 and 7.

.DESCRIPTION
  Two kinds of checks.

  native       Needs nothing but PowerShell: piped scripts, error text (identical to cuffc's),
               UTF-8 / Korean text and paths, command-line flags, resource limits, and a
               side-by-side comparison of cuffsh against cuffc over every .cuff file the
               project ships.

  interactive  Drives cuffsh on a real pseudo-terminal and checks what ends up on screen:
               typing, wrapping, Korean text, cursor keys, Shift+Enter, paste, history,
               Ctrl+C, input(), entries taller than the window ... plus a fuzzer.
               Needs Python 3.8+ and, on Windows (10 version 1809 or newer, for ConPTY):
                   python -m pip install pywinpty pyte wcwidth
               (-InstallDeps does this for you). Without Python the native checks still run.

  Works on Windows PowerShell 5.1 and PowerShell 7. The file is plain ASCII on purpose: 5.1
  reads BOM-less files in the ANSI code page, so non-ASCII text is built from code points.

.PARAMETER Cuffsh
  Path to cuffsh.exe. Default: ..\cuffsh.exe (next to build.bat). Alias: -Exe.

.PARAMETER Cuffc
  Path to cuffc.exe, used for the comparison tests. Default: <project root>\cuffc.exe.
  Without it those tests are skipped.

.PARAMETER Build
  Build cuffsh first (..\build.bat on Windows, ../build.sh elsewhere; needs g++ on PATH).

.PARAMETER Suite
  all (default) | native | editor | session | fuzz

.PARAMETER InstallDeps
  pip install the Python packages the interactive suites need.

.PARAMETER FuzzTrials
  Number of random sessions for the fuzzer (default 40).

.PARAMETER TimeScale
  Multiplies every wait in the interactive suites (default 2.5 on Windows, 1 elsewhere).
  Raise it on a slow machine or if checks fail only intermittently.

.PARAMETER Python
  Python to use (a path or a command name). Default: py -3, python, python3.

.PARAMETER SkipDifferential
  Skip the cuffc-vs-cuffsh comparison over the shipped scripts (the slow part of "native").

.PARAMETER NoChecklist
  Don't print the short manual checklist at the end.

.PARAMETER Strict
  Treat a skipped suite (for example no Python installed) as a failure.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File cli\tests\run_tests.ps1 -Build -InstallDeps
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File cli\tests\run_tests.ps1 -Suite native
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File cli\tests\run_tests.ps1 -Suite editor -TimeScale 4
#>
[CmdletBinding()]
param(
    [Alias('Exe')]
    [string]$Cuffsh,
    [string]$Cuffc,
    [switch]$Build,
    [ValidateSet('all', 'native', 'editor', 'session', 'fuzz')]
    [string]$Suite = 'all',
    [switch]$InstallDeps,
    [int]$FuzzTrials = 40,
    [double]$TimeScale = 0,
    [string]$Python,
    [switch]$SkipDifferential,
    [switch]$NoChecklist,
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'

# Parent folder, but never empty: a drive root (or a script copied somewhere shallow) has no parent,
# and Split-Path would then return an empty string.
function Get-ParentDir([string]$Path) {
    $trimmed = $Path.TrimEnd([char]'\', [char]'/')
    if ($trimmed -eq '') { return $Path }
    $p = [System.IO.Path]::GetDirectoryName($trimmed)
    if ([string]::IsNullOrEmpty($p)) { return $Path }
    return $p
}

$testsDir = $PSScriptRoot
if ([string]::IsNullOrEmpty($testsDir)) { $testsDir = (Get-Location).Path }
$cliDir   = Get-ParentDir $testsDir
$root     = Get-ParentDir $cliDir
$isWin    = ([System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT)

# Python on Windows would otherwise print with the console code page, and the tests print Korean.
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
try { [Console]::OutputEncoding = (New-Object System.Text.UTF8Encoding($false)) } catch { }

# Run a native command for its exit code and text. (On Windows PowerShell 5.1, stderr output
# from a native command becomes an error record that $ErrorActionPreference = 'Stop' would
# turn into a terminating error, hence the temporary relaxation.)
function Invoke-Native([string]$File, [string[]]$ArgList = @()) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $code = -1
    $text = ''
    try {
        $lines = & $File @ArgList 2>&1 | ForEach-Object { [string]$_ }
        $code = $LASTEXITCODE
        $text = ($lines -join "`n")
    } catch {
        $text = [string]$_
    } finally {
        $ErrorActionPreference = $old
    }
    return [pscustomobject]@{ Code = $code; Text = $text }
}

# ---------------------------------------------------------------- locate binaries
if ($Build) {
    Write-Host 'Building cuffsh ...' -ForegroundColor Cyan
    if ($isWin) { $r = Invoke-Native (Join-Path $cliDir 'build.bat') }
    else        { $r = Invoke-Native 'bash' @((Join-Path $cliDir 'build.sh')) }
    if ($r.Text) { Write-Host $r.Text }
    if ($r.Code -ne 0) { Write-Host 'Build failed (a g++ on PATH is needed: mingw-w64 on Windows).' -ForegroundColor Red; exit 1 }
}

if (-not $Cuffsh) {
    foreach ($n in @('cuffsh.exe', 'cuffsh')) {
        $c = Join-Path $cliDir $n
        if (Test-Path -LiteralPath $c) { $Cuffsh = $c; break }
    }
}
if (-not $Cuffsh -or -not (Test-Path -LiteralPath $Cuffsh)) {
    Write-Host 'cuffsh not found. Build it first (cli\build.bat), or pass -Cuffsh <path>, or use -Build.' -ForegroundColor Red
    exit 2
}
$Cuffsh = (Resolve-Path -LiteralPath $Cuffsh).Path

if (-not $Cuffc) {
    foreach ($n in @('cuffc.exe', 'cuffc')) {
        $c = Join-Path $root $n
        if (Test-Path -LiteralPath $c) { $Cuffc = $c; break }
    }
}
if ($Cuffc -and (Test-Path -LiteralPath $Cuffc)) { $Cuffc = (Resolve-Path -LiteralPath $Cuffc).Path } else { $Cuffc = $null }

# ---------------------------------------------------------------- helpers
$utf8 = New-Object System.Text.UTF8Encoding($false)
$script:results = New-Object System.Collections.ArrayList

function Check {
    param([string]$Name, [bool]$Ok, $Detail = '')
    [void]$script:results.Add([pscustomobject]@{ Name = $Name; Ok = $Ok })
    if ($Ok) {
        Write-Host ('PASS  ' + $Name) -ForegroundColor Green
    } else {
        Write-Host ('FAIL  ' + $Name) -ForegroundColor Red
        $d = ([string]$Detail).Trim()
        if ($d.Length -gt 400) { $d = $d.Substring(0, 400) + ' ...' }
        if ($d -ne '') { Write-Host ('      ' + ($d -replace "`r?`n", "`n      ")) -ForegroundColor DarkYellow }
    }
}

function Skip([string]$Name, [string]$Why) {
    Write-Host ('SKIP  ' + $Name + '  (' + $Why + ')') -ForegroundColor DarkGray
}

# Windows command-line quoting for one argument.
function Quote-Arg([string]$a) {
    if ($a -ne '' -and $a -notmatch '[\s"]') { return $a }
    $s = $a -replace '(\\*)"', '$1$1\"'
    $s = $s -replace '(\\+)$', '$1$1'
    return '"' + $s + '"'
}

# NOTE: PowerShell's -eq / -match ignore case. Anything comparing program output uses the
# case-sensitive forms (-ceq, -cne, -cmatch) so 'error:' can't pass for 'ERROR:'.
function Norm([string]$s) { return ($s -replace "`r`n", "`n") }

# Runs a program with exact bytes on stdin (no BOM, no console encoding games),
# captures stdout/stderr as UTF-8, and kills it if it runs past the timeout.
function Invoke-Proc {
    param(
        [string]$Exe,
        [string[]]$ArgList = @(),
        [byte[]]$InputBytes = $null,
        [string]$WorkDir = $null,
        [int]$TimeoutMs = 30000
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.Arguments = (($ArgList | ForEach-Object { Quote-Arg $_ }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.StandardOutputEncoding = $utf8
    $psi.StandardErrorEncoding = $utf8
    if ($WorkDir) { $psi.WorkingDirectory = $WorkDir }

    $p = [System.Diagnostics.Process]::Start($psi)
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    try {
        if ($InputBytes -and $InputBytes.Length -gt 0) {
            $p.StandardInput.BaseStream.Write($InputBytes, 0, $InputBytes.Length)
            $p.StandardInput.BaseStream.Flush()
        }
        $p.StandardInput.Close()
    } catch {
        # The program may legitimately stop reading (e.g. input too large) and exit first.
    }
    $timedOut = $false
    if (-not $p.WaitForExit($TimeoutMs)) {
        $timedOut = $true
        try { $p.Kill() } catch { }
        $p.WaitForExit()
    }
    $code = $p.ExitCode
    return [pscustomobject]@{
        ExitCode = $code
        Out      = (Norm $outTask.Result)
        Err      = (Norm $errTask.Result)
        TimedOut = $timedOut
    }
}

function Bytes([string]$s) { return $utf8.GetBytes($s) }

# Non-ASCII test text, built from code points (see the note in .DESCRIPTION).
$hangul = ([string][char]0xD55C) + ([string][char]0xAE00)   # two Hangul syllables


# ================================================================= interactive suites
# One row per suite for the final summary.
$script:rows = New-Object System.Collections.ArrayList
function Add-Row([string]$Name, [string]$Status, [string]$Detail) {
    [void]$script:rows.Add([pscustomobject]@{ Name = $Name; Status = $Status; Detail = $Detail })
}

function Find-Python {
    $candidates = New-Object System.Collections.ArrayList
    if ($Python) {
        [void]$candidates.Add(@{ File = $Python; Pre = @() })
    } else {
        [void]$candidates.Add(@{ File = 'py'; Pre = @('-3') })
        [void]$candidates.Add(@{ File = 'python'; Pre = @() })
        [void]$candidates.Add(@{ File = 'python3'; Pre = @() })
    }
    foreach ($c in $candidates) {
        if (-not (Get-Command $c.File -ErrorAction SilentlyContinue)) { continue }
        # (the Microsoft Store "python.exe" stub exists but only prints a hint: so run it to be sure)
        $r = Invoke-Native $c.File ($c.Pre + @('-c', 'import sys; print(sys.version_info[0] * 100 + sys.version_info[1])'))
        $v = 0
        if ($r.Code -eq 0 -and [int]::TryParse($r.Text.Trim(), [ref]$v) -and $v -ge 308) { return $c }
    }
    return $null
}

function Test-PythonDeps($py) {
    $imports = 'import pyte, wcwidth'
    if ($isWin) { $imports = 'import pyte, wcwidth, winpty' }
    $r = Invoke-Native $py.File ($py.Pre + @('-c', $imports))
    return ($r.Code -eq 0)
}

function Invoke-PythonSuite($py, [string]$Label, [string]$ScriptName, [string[]]$ExtraArgs) {
    Write-Host ''
    Write-Host ('== ' + $Label + ' ==') -ForegroundColor Cyan
    $scriptPath = Join-Path $testsDir $ScriptName
    if (-not (Test-Path -LiteralPath $scriptPath)) {
        Write-Host ('  ' + $ScriptName + ' is not next to this script - copy the whole cli\tests folder (or use the zip).') -ForegroundColor Yellow
        Add-Row $Label 'SKIPPED' ($ScriptName + ' not found next to run_tests.ps1')
        return
    }
    $lines = New-Object System.Collections.ArrayList
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $code = 1
    try {
        # streamed, so a long suite shows progress instead of looking hung
        & $py.File @($py.Pre + @($scriptPath, $Cuffsh) + $ExtraArgs) 2>&1 | ForEach-Object {
            $line = [string]$_
            Write-Host $line
            [void]$lines.Add($line)
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $old
    }
    $last = ''
    for ($i = $lines.Count - 1; $i -ge 0; $i--) { if ($lines[$i].Trim() -ne '') { $last = $lines[$i].Trim(); break } }
    $nFail = @($lines | Where-Object { $_ -clike 'FAIL *' }).Count
    $detail = $last
    if ($nFail -gt 0) { $detail = $last + ' (' + $nFail + ' failing checks above)' }
    if ($code -eq 0) { Add-Row $Label 'PASS' $detail } else { Add-Row $Label 'FAIL' $detail }
}

function Run-Interactive([string[]]$Which) {
    $py = Find-Python
    if (-not $py) {
        Write-Host ''
        Write-Host 'No usable Python 3.8+ found: skipping the interactive suites.' -ForegroundColor Yellow
        Write-Host '  Install Python from https://www.python.org/downloads/ (tick "Add python.exe to PATH"), then re-run.' -ForegroundColor Yellow
        foreach ($w in $Which) { Add-Row ('interactive: ' + $w) 'SKIPPED' 'Python not found' }
        return
    }
    if ($isWin -and [System.Environment]::OSVersion.Version.Build -lt 17763) {
        Write-Host ''
        Write-Host 'The interactive suites need ConPTY (Windows 10 version 1809, build 17763, or newer): skipping.' -ForegroundColor Yellow
        foreach ($w in $Which) { Add-Row ('interactive: ' + $w) 'SKIPPED' 'Windows too old for ConPTY' }
        return
    }
    if (-not (Test-PythonDeps $py)) {
        $pkgs = @('pyte', 'wcwidth')
        if ($isWin) { $pkgs = @('pywinpty', 'pyte', 'wcwidth') }
        $pipArgs = @('-m', 'pip', 'install', '--user') + $pkgs
        if ($InstallDeps) {
            Write-Host ('Installing Python packages: ' + ($pkgs -join ' ')) -ForegroundColor Cyan
            $r = Invoke-Native $py.File ($py.Pre + $pipArgs)
            if ($r.Text) { Write-Host $r.Text }
        }
        if (-not (Test-PythonDeps $py)) {
            Write-Host ''
            Write-Host ('Missing Python packages. Install them with:') -ForegroundColor Yellow
            Write-Host ('  ' + $py.File + ' ' + (($py.Pre + $pipArgs) -join ' ')) -ForegroundColor Yellow
            Write-Host '  (or re-run this script with -InstallDeps)' -ForegroundColor Yellow
            foreach ($w in $Which) { Add-Row ('interactive: ' + $w) 'SKIPPED' 'Python packages missing' }
            return
        }
    }
    if ($TimeScale -gt 0) { $env:CUFFSH_TEST_SCALE = [string]$TimeScale }

    foreach ($w in $Which) {
        if ($w -eq 'editor')  { Invoke-PythonSuite $py 'interactive: editor (keys, wrapping, paste, history, Ctrl+C ...)' 'test_editor.py' @() }
        if ($w -eq 'session') { Invoke-PythonSuite $py 'interactive: session (input(), flags, randomized editing ...)' 'test_session.py' @() }
        if ($w -eq 'fuzz')    { Invoke-PythonSuite $py ('interactive: fuzz (' + $FuzzTrials + ' random sessions)') 'fuzz.py' @([string]$FuzzTrials, '1') }
    }
}

# ================================================================= go
$runNative = ($Suite -eq 'all' -or $Suite -eq 'native')
$interactiveWhich = @()
if ($Suite -eq 'all') { $interactiveWhich = @('editor', 'session', 'fuzz') }
elseif ($Suite -ne 'native') { $interactiveWhich = @($Suite) }

if ($runNative) {
    # ================================================================= tests
    Write-Host ''
    Write-Host ('cuffsh : ' + $Cuffsh) -ForegroundColor Cyan
    if ($Cuffc) { Write-Host ('cuffc  : ' + $Cuffc) -ForegroundColor Cyan } else { Write-Host 'cuffc  : (not found - comparison tests will be skipped)' -ForegroundColor DarkGray }
    Write-Host ''

    # ---- basics
    $r = Invoke-Proc $Cuffsh @('--version')
    Check '--version prints a version and exits 0' (($r.ExitCode -eq 0) -and ($r.Out -cmatch '^cuffsh \d+\.\d+\.\d+')) $r.Out

    $r = Invoke-Proc $Cuffsh @('--help')
    Check '--help exits 0 and documents Shift+Enter' (($r.ExitCode -eq 0) -and ($r.Out -cmatch 'Shift\+Enter') -and ($r.Out -cmatch '--timeout')) $r.Out

    # ---- piped input runs as one script (no prompt, no banner)
    $src = "set number x to 4`nprint(x * 2)`nif x < 10 do:`n    print(`"small`")`nend`n"
    $r = Invoke-Proc $Cuffsh @() (Bytes $src)
    Check 'piped script runs as one script' (($r.ExitCode -eq 0) -and ($r.Out -ceq "8`nsmall`n") -and ($r.Err -ceq '')) ($r.Out + '|' + $r.Err)

    $r = Invoke-Proc $Cuffsh @() (Bytes "print(1 + 2)")
    Check 'piped script without a trailing newline works' ($r.Out -ceq "3`n") $r.Out

    $r = Invoke-Proc $Cuffsh @() (@(0xEF, 0xBB, 0xBF) + [byte[]](Bytes "print(5)`n"))
    Check 'UTF-8 BOM at the start of piped input is ignored' ($r.Out -ceq "5`n" -and $r.Err -ceq '') ($r.Out + '|' + $r.Err)

    # ---- UTF-8 text in and out
    $r = Invoke-Proc $Cuffsh @() (Bytes ('print("' + $hangul + '!")' + "`n"))
    Check 'Korean text round-trips (UTF-8 in, UTF-8 out)' ($r.Out -ceq ($hangul + "!`n")) $r.Out

    # ---- errors are the engine's own, printed the way cuffc prints them
    $r = Invoke-Proc $Cuffsh @() (Bytes "print(1 / 0)`n")
    Check 'runtime error: "ERROR: [E4006] ..." on stderr, stdout empty' (($r.Err -cmatch '^ERROR: \[E4006\] Runtime Error at line 1') -and ($r.Out -ceq '')) ($r.Err)

    $r = Invoke-Proc $Cuffsh @() (Bytes "if 1 is 1 do:`n")
    Check 'incomplete block: engine syntax error ("expected end"), no guessing' (($r.Err -cmatch '^ERROR: \[E2001\] Syntax Error') -and ($r.Err -cmatch "expected 'end'")) $r.Err

    if ($Cuffc) {
        foreach ($bad in @("print(1 / 0)`n", "if 1 is 1 do:`n", "print(undefined_name)`n", "set number x to `"a`" + 1`n")) {
            $a = Invoke-Proc $Cuffc @() (Bytes $bad)
            $b = Invoke-Proc $Cuffsh @() (Bytes $bad)
            Check ('error text identical to cuffc: ' + ($bad.Trim() -replace "`n", ' / ')) (($a.Err -ceq $b.Err) -and ($a.Out -ceq $b.Out)) ("cuffc : " + $a.Err + "cuffsh: " + $b.Err)
        }
    }

    # ---- startup file argument
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('cuffsh_tests_' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    try {
        $f = Join-Path $tmp 'hello.cuff'
        [System.IO.File]::WriteAllBytes($f, (Bytes "print(`"from file`")`n"))
        $r = Invoke-Proc $Cuffsh @($f) (Bytes "print(`"from stdin`")`n")
        Check 'startup file runs first, then the piped script' ($r.Out -ceq "from file`nfrom stdin`n") $r.Out

        $r = Invoke-Proc $Cuffsh @((Join-Path $tmp 'missing.cuff')) (Bytes "print(7)`n")
        Check 'missing startup file: clear error, shell still runs the rest' (($r.Err -cmatch 'Cannot open file') -and ($r.Out -ceq "7`n")) ($r.Err + '|' + $r.Out)

        $bom = Join-Path $tmp 'bom.cuff'
        [System.IO.File]::WriteAllBytes($bom, ([byte[]]@(0xEF, 0xBB, 0xBF) + [byte[]](Bytes "print(`"bom ok`")`n")))
        $r = Invoke-Proc $Cuffsh @($bom) $null
        Check 'startup file with a UTF-8 BOM runs' (($r.Out -ceq "bom ok`n") -and ($r.Err -ceq '')) ($r.Out + '|' + $r.Err)

        # a path with non-ASCII folder and file names (checks the wide-character path handling)
        $kdir = Join-Path $tmp ($hangul + '_dir')
        New-Item -ItemType Directory -Path $kdir | Out-Null
        $kf = Join-Path $kdir ($hangul + '.cuff')
        [System.IO.File]::WriteAllBytes($kf, (Bytes ('print("' + $hangul + ' path ok")' + "`n")))
        $r = Invoke-Proc $Cuffsh @($kf) $null
        Check 'startup file in a Korean-named folder, with a Korean file name' ($r.Out -ceq ($hangul + " path ok`n")) ($r.Out + '|' + $r.Err)

        $spaced = Join-Path $tmp 'a folder with spaces'
        New-Item -ItemType Directory -Path $spaced | Out-Null
        $sf = Join-Path $spaced 'x y.cuff'
        [System.IO.File]::WriteAllBytes($sf, (Bytes "print(`"spaces ok`")`n"))
        $r = Invoke-Proc $Cuffsh @($sf) $null
        Check 'startup file path containing spaces' ($r.Out -ceq "spaces ok`n") ($r.Out + '|' + $r.Err)

        # ---- oversized input is refused, not buffered without limit
        $chunk = [System.Text.Encoding]::ASCII.GetBytes(('a' * 1048576))
        $big = New-Object byte[] (17 * 1048576)
        for ($i = 0; $i -lt 17; $i++) { [System.Buffer]::BlockCopy($chunk, 0, $big, $i * 1048576, 1048576) }
        $r = Invoke-Proc -Exe $Cuffsh -InputBytes $big -TimeoutMs 60000
        Check 'input over the 16 MiB limit is refused with a clear message' (($r.Err -cmatch 'too large') -and (-not $r.TimedOut)) ($r.Err)
    }
    finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }

    # ---- command-line flags
    $r = Invoke-Proc $Cuffsh @('--bogus'); Check 'unknown option -> exit 2 and a message' (($r.ExitCode -eq 2) -and ($r.Err -cmatch 'unknown option')) $r.Err
    $r = Invoke-Proc $Cuffsh @('--timeout'); Check '--timeout with no value -> exit 2, one clear message' (($r.ExitCode -eq 2) -and ($r.Err -cmatch 'requires a value') -and (($r.Err.Trim() -split "`n").Count -eq 1)) $r.Err
    $r = Invoke-Proc $Cuffsh @('--timeout', 'abc'); Check '--timeout abc -> exit 2' ($r.ExitCode -eq 2) $r.Err
    $r = Invoke-Proc $Cuffsh @('--timeout', '-5'); Check '--timeout -5 -> exit 2' ($r.ExitCode -eq 2) $r.Err
    $r = Invoke-Proc $Cuffsh @('--max-steps', 'x'); Check '--max-steps x -> exit 2' ($r.ExitCode -eq 2) $r.Err
    $r = Invoke-Proc $Cuffsh @('a.cuff', 'b.cuff'); Check 'two script files -> exit 2' (($r.ExitCode -eq 2) -and ($r.Err -cmatch 'only one')) $r.Err

    # ---- resource limits come from the engine and the session survives them
    $loop = "set number c to 0`nloop while true do:`n    change c to c + 1`nend`nprint(`"after`")`n"
    $t0 = Get-Date
    $r = Invoke-Proc -Exe $Cuffsh -ArgList @('--timeout', '400') -InputBytes (Bytes $loop) -TimeoutMs 20000
    $el = ((Get-Date) - $t0).TotalSeconds
    Check '--timeout stops a runaway loop (engine error E6002) in time' (($r.Err -cmatch '\[E6002\]') -and (-not $r.TimedOut) -and ($el -lt 15)) ("took " + [math]::Round($el, 1) + "s; " + $r.Err)

    $steps = "set number c to 0`nloop repeat i to 1 ~ 1000 do:`n    change c to c + 1`nend`nprint(c)`n"
    $r = Invoke-Proc $Cuffsh @('--max-steps', '50') (Bytes $steps)
    Check '--max-steps stops a loop (engine error E6001)' ($r.Err -cmatch '\[E6001\]') $r.Err
    $r = Invoke-Proc $Cuffsh @('--max-steps', '100000') (Bytes $steps)
    Check '--max-steps with a generous limit lets the same loop finish' (($r.Out -ceq "1000`n") -and ($r.Err -ceq '')) ($r.Out + '|' + $r.Err)

    $deep = "set returnable func f(n) do:`n    return f(n + 1)`nend`nprint(f(0))`n"
    $r = Invoke-Proc -Exe $Cuffsh -ArgList @('--timeout', '0') -InputBytes (Bytes $deep) -TimeoutMs 60000
    Check 'runaway recursion ends with the engine''s stack error, not a crash' (($r.Err -cmatch '\[E4017\]') -and (-not $r.TimedOut) -and ($r.ExitCode -eq 0)) ("exit " + $r.ExitCode + "; " + $r.Err)

    # ---- the same scripts give the same output under cuffc and cuffsh
    if ($SkipDifferential) {
        Skip 'cuffc vs cuffsh over the shipped scripts' '-SkipDifferential'
    } elseif (-not $Cuffc) {
        Skip 'cuffc vs cuffsh over the shipped scripts' 'cuffc not found; build it (make -f Makefile.win) or pass -Cuffc'
    } else {
        # Not comparable by design: random dice rolls, and a stack-exhaustion error
        # whose column depends on exactly how much stack each program's frames use.
        $nondeterministic = @('08_dlc_libraries.cuff', 'stack_budget_nested_recursion.cuff')
        $groups = @(
            @{ Dir = 'tests\cases';          Stdin = $true  },
            @{ Dir = 'tests\errors';         Stdin = $false },
            @{ Dir = 'examples';             Stdin = $true  },
            @{ Dir = 'examples\error_cases'; Stdin = $false }
        )
        $nFile = 0; $nStdin = 0; $diffs = New-Object System.Collections.ArrayList
        foreach ($g in $groups) {
            $dir = Join-Path $root $g.Dir
            if (-not (Test-Path -LiteralPath $dir)) { continue }
            Get-ChildItem -LiteralPath $dir -Filter '*.cuff' -File | Sort-Object Name | ForEach-Object {
                if ($nondeterministic -contains $_.Name) { return }
                # as a script file, run from its own folder, no stdin
                $a = Invoke-Proc $Cuffc  @($_.Name) $null $dir
                $b = Invoke-Proc $Cuffsh @('--timeout', '0', $_.Name) $null $dir
                $nFile++
                if (($a.Out -cne $b.Out) -or ($a.Err -cne $b.Err)) { [void]$diffs.Add(($g.Dir + '\' + $_.Name + '  (as a file)')) }
                # as a script on stdin (how cuffc reads a script when given no file)
                if ($g.Stdin) {
                    $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
                    $a = Invoke-Proc $Cuffc  @() $bytes $root
                    $b = Invoke-Proc $Cuffsh @('--timeout', '0') $bytes $root
                    $nStdin++
                    if (($a.Out -cne $b.Out) -or ($a.Err -cne $b.Err)) { [void]$diffs.Add(($g.Dir + '\' + $_.Name + '  (on stdin)')) }
                }
            }
        }
        if ($nFile -eq 0) {
            Skip 'cuffsh output == cuffc output for every shipped script' 'no .cuff scripts found: run this script from inside the project (cli\tests) so tests\ and examples\ are found'
        } else {
            Check ('cuffsh output == cuffc output for every shipped script (' + $nFile + ' as files, ' + $nStdin + ' on stdin)') ($diffs.Count -eq 0) (($diffs | Select-Object -First 8) -join "`n")
        }
    }
    $nFailed = @($script:results | Where-Object { -not $_.Ok })
    $nDetail = '{0}/{1} passed' -f ($script:results.Count - $nFailed.Count), $script:results.Count
    if ($nFailed.Count -eq 0) { Add-Row 'native checks' 'PASS' $nDetail } else { Add-Row 'native checks' 'FAIL' $nDetail }
}

if ($interactiveWhich.Count -gt 0) { Run-Interactive $interactiveWhich }

# ================================================================= summary
Write-Host ''
Write-Host '================ summary ================' -ForegroundColor Cyan
$anyFail = $false; $anySkip = $false
foreach ($row in $script:rows) {
    $color = 'Green'
    if ($row.Status -eq 'FAIL') { $color = 'Red'; $anyFail = $true }
    elseif ($row.Status -eq 'SKIPPED') { $color = 'Yellow'; $anySkip = $true }
    Write-Host ('{0,-8} {1}  [{2}]' -f $row.Status, $row.Name, $row.Detail) -ForegroundColor $color
}

if (-not $NoChecklist) {
    Write-Host @'

Not covered above - needs a person at a real console (cmd, PowerShell or Windows Terminal):
  [ ] Shift+Enter in YOUR terminal app (the automated check feeds ConPTY a Shift+Enter key event; this confirms the app sends it)
  [ ] type Korean with the IME (composition, Backspace on a syllable) at the prompt
  [ ] paste several lines from the clipboard: ONE entry, not run line by line; paste more lines than the window is
      tall, then move the cursor - the screen must not fill with repeated copies of your code
  [ ] resize the window while typing, then keep typing
  [ ] Git Bash / mintty: running cuffsh.exe says to use winpty instead of hanging
'@
}

if ($anyFail) { Write-Host ''; Write-Host 'Some checks FAILED.' -ForegroundColor Red; exit 1 }
if ($anySkip -and $Strict) { Write-Host ''; Write-Host 'Some suites were skipped and -Strict was given.' -ForegroundColor Red; exit 1 }
Write-Host ''
if ($anySkip) { Write-Host 'Everything that ran passed (some suites were skipped - see above).' -ForegroundColor Yellow }
else { Write-Host 'All checks passed.' -ForegroundColor Green }
exit 0
