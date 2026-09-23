$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = "-NoProfile -NoExit -ExecutionPolicy Bypass -File `"$PSCommandPath`" $($args -join ' ')"
    Start-Process powershell -Verb RunAs -ArgumentList $arguments
    Exit
}

$ErrorActionPreference = "Stop"

try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
} catch {}

$RealRoot = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..")).Path

function Test-HasNonAscii {
    param ([string]$Text)
    return ($Text -match '[^\x00-\x7F]')
}

$RunRoot = $RealRoot
if (Test-HasNonAscii $RealRoot) {
    $JunctionRoot = Join-Path $env:TEMP "cuffscript_ci_run"
    if (Test-HasNonAscii $JunctionRoot) {
        Write-Output "WARNING: TEMP path also contains non-ASCII characters; junction workaround is not possible."
        Write-Output "If tests keep failing, move the project to an ASCII-only path (e.g. C:\dev\cuffscript)."
    }
    else {
        if (Test-Path $JunctionRoot) {
            $existingTarget = (Get-Item $JunctionRoot).Target
            if ($existingTarget -ne $RealRoot) {
                (Get-Item $JunctionRoot).Delete()
            }
        }
        if (-not (Test-Path $JunctionRoot)) {
            cmd /c mklink /J "$JunctionRoot" "$RealRoot" | Out-Null
        }
        $RunRoot = $JunctionRoot
        Write-Output "Non-ASCII path detected: running tests via junction '$JunctionRoot' (points back to the real project folder; no files are copied)."
    }
}

Set-Location $RunRoot

$BIN = "./cuffc"
if (-not (Test-Path $BIN) -and (Test-Path "${BIN}.exe")) {
    $BIN = "${BIN}.exe"
}
$PASS = 0
$FAIL = 0

function check_bin {
    if (-not (Test-Path $BIN)) {
        [Console]::Error.WriteLine("cuffc not found -- run 'make' first")
        Exit 1
    }
    $script:BIN = (Resolve-Path $BIN).Path
}

function Test-GxxAvailable {
    return [bool](Get-Command g++ -ErrorAction SilentlyContinue)
}

function Invoke-WithTimeout {
    param (
        [int]$Seconds,
        [string]$Command,
        [string]$Argument
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Command
    $psi.WorkingDirectory = (Get-Location).Path
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    try {
        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
        $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    } catch {}

    if ([string]::IsNullOrEmpty($Argument)) {
        $psi.Arguments = ""
    } else {
        $psi.Arguments = '"' + $Argument + '"'
    }

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    try {
        [void]$proc.Start()

        $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
        $stderrTask = $proc.StandardError.ReadToEndAsync()

        $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
        $timedOut = $false

        while ($true) {
            $proc.Refresh()
            if ($proc.HasExited) { break }
            if ([DateTime]::UtcNow -ge $deadline) { $timedOut = $true; break }
            Start-Sleep -Milliseconds 25
        }

        if ($timedOut -and -not $proc.HasExited) {
            try { $proc.Kill() } catch {}
            try { [void]$proc.WaitForExit() } catch {}
            $exitCode = 124
        } else {
            [void]$proc.WaitForExit()
            $proc.Refresh()
            $exitCode = [int]$proc.ExitCode
        }

        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        $combined = "$stdout$stderr"

        return [PSCustomObject]@{ Output = $combined; ExitCode = $exitCode }
    }
    finally {
        $proc.Dispose()
    }
}

function run_success_case {
    param ([string]$cuff)
    $expected = $cuff -replace '\.cuff$', '.expected'
    $res = Invoke-WithTimeout -Seconds 10 -Command $BIN -Argument $cuff
    $actual = ($res.Output -replace "`r`n", "`n").TrimEnd("`n")
    $code = $res.ExitCode
    if ($code -ne 0) {
        Write-Output "FAIL (exit code $code): $cuff"
        Write-Output $actual
        $script:FAIL++
        return
    }
    if (-not (Test-Path $expected)) {
        $script:PASS++
        return
    }
    $expected_content = ((Get-Content $expected -Raw -Encoding UTF8) -replace "`r`n", "`n").TrimEnd("`n")
    if ($actual -ne $expected_content) {
        Write-Output "FAIL (output mismatch): $cuff"
        Compare-Object ($actual -split "`n") ($expected_content -split "`n")
        $script:FAIL++
        return
    }
    $script:PASS++
}

function run_error_case {
    param ([string]$cuff)
    $expected_code_file = $cuff -replace '\.cuff$', '.expected_code'
    $res = Invoke-WithTimeout -Seconds 10 -Command $BIN -Argument $cuff
    $actual = $res.Output
    $code = $res.ExitCode
    if ($code -eq 0) {
        Write-Output "FAIL (expected nonzero exit): $cuff"
        $script:FAIL++
        return
    }
    if (Test-Path $expected_code_file) {
        $expected_code = (Get-Content $expected_code_file -Raw -Encoding UTF8).Trim()
        $escaped = [regex]::Escape("[$expected_code]")
        if ($actual -notmatch $escaped) {
            Write-Output "FAIL (expected $expected_code not found): $cuff"
            Write-Output $actual
            $script:FAIL++
            return
        }
    }
    $script:PASS++
}

check_bin

Write-Output "== tests/unit (C++ unit tests) =="
if (-not (Test-GxxAvailable)) {
    Write-Output "SKIP: g++ not found on PATH -- cannot build tests/unit/*.cpp"
}
else {
    $unit_files = Get-ChildItem -Path "tests/unit" -Filter "*.cpp" -ErrorAction SilentlyContinue
    foreach ($src_file in $unit_files) {
        $src = $src_file.FullName
        $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
        $unitBin = Join-Path $env:TEMP "cuff_unit_${base}.exe"
        $build_log = Join-Path $env:TEMP "cuff_unit_build.log"
        $process = Start-Process g++ -ArgumentList "-std=c++17", "-Wall", "-Wextra", "-O2", "-I.", "`"$src`"", "-o", "`"$unitBin`"", "-Wl,--stack=16777216" -RedirectStandardError $build_log -NoNewWindow -PassThru -Wait
        if ($process.ExitCode -ne 0) {
            Write-Output "FAIL (build): $src"
            if (Test-Path $build_log) { Get-Content $build_log }
            $script:FAIL++
            continue
        }
        $res = Invoke-WithTimeout -Seconds 60 -Command $unitBin -Argument ""
        $out = $res.Output
        if ($res.ExitCode -eq 0) {
            $lines = $out -split '\r?\n' | Where-Object { $_ -ne "" }
            $last_line = if ($lines.Count -gt 0) { $lines[-1] } else { "" }
            Write-Output "  $($src_file.Name): $last_line"
            $script:PASS++
        } else {
            Write-Output "FAIL: $src"
            Write-Output $out
            $script:FAIL++
        }
    }
}

Write-Output "== tests/cases (output diff) =="
$cases_files = Get-ChildItem -Path "tests/cases" -Filter "*.cuff" -ErrorAction SilentlyContinue
foreach ($f in $cases_files) { run_success_case $f.FullName }

Write-Output "== tests/errors (error code check) =="
$errors_files = Get-ChildItem -Path "tests/errors" -Filter "*.cuff" -ErrorAction SilentlyContinue
foreach ($f in $errors_files) { run_error_case $f.FullName }

Write-Output "== examples (output diff) =="
$examples_files = Get-ChildItem -Path "examples" -Filter "*.cuff" -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^[01]' }
foreach ($f in $examples_files) { run_success_case $f.FullName }

Write-Output "== examples/error_cases (error code check) =="
$ex_errors_files = Get-ChildItem -Path "examples/error_cases" -Filter "*.cuff" -ErrorAction SilentlyContinue
foreach ($f in $ex_errors_files) { run_error_case $f.FullName }

Write-Output ""
Write-Output "$PASS passed, $FAIL failed"
if ($FAIL -ne 0) { Exit 1 } else { Exit 0 }
