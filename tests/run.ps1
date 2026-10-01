# PowerShell equivalent of tests/run.sh, for running the suite on Windows
# without WSL/Git Bash. Mirrors its structure and output format exactly
# (same section headers, same "$pass passed, $fail failed" summary line, same
# exit code convention: 0 only if everything passed) so CI or a developer can
# use whichever is convenient for their platform.
#
# Usage:  powershell -File tests\run.ps1
# (or, from inside PowerShell, just: .\tests\run.ps1)

$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

$Bin = '.\cuffc.exe'
$Pass = 0
$Fail = 0

# Runs a process with a timeout, capturing combined stdout+stderr as a single
# string and the exit code -- .NET's Process class rather than a built-in
# `timeout` command, since PowerShell has no direct equivalent that also
# captures output the way `timeout 10 cmd` does in bash.
function Invoke-WithTimeout {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [int]$TimeoutSeconds = 10
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    foreach ($a in $Arguments) { $psi.ArgumentList.Add($a) }
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $null = $proc.Start()
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        try { $proc.Kill() } catch {}
        return [PSCustomObject]@{ Output = "(timed out after ${TimeoutSeconds}s)"; ExitCode = 124 }
    }
    $proc.WaitForExit()  # let the async reads finish
    $combined = ($stdoutTask.Result + $stderrTask.Result)
    return [PSCustomObject]@{ Output = $combined.TrimEnd("`r", "`n"); ExitCode = $proc.ExitCode }
}

function Test-Binary {
    if (-not (Test-Path $Bin)) {
        Write-Error "cuffc.exe not found -- run 'mingw32-make -f Makefile.win' (or your equivalent) first"
        exit 1
    }
}

function Invoke-SuccessCase {
    param([string]$CuffFile)
    $expected = [System.IO.Path]::ChangeExtension($CuffFile, '.expected')
    $result = Invoke-WithTimeout -FilePath $Bin -Arguments @($CuffFile)
    if ($result.ExitCode -ne 0) {
        Write-Host "FAIL (exit $($result.ExitCode)): $CuffFile"
        Write-Host $result.Output
        $script:Fail++
        return
    }
    if (-not (Test-Path $expected)) {
        $script:Pass++
        return
    }
    $expectedText = (Get-Content -Raw $expected).TrimEnd("`r", "`n")
    if ($result.Output -ne $expectedText) {
        Write-Host "FAIL (output mismatch): $CuffFile"
        Write-Host "--- expected ---"
        Write-Host $expectedText
        Write-Host "--- actual ---"
        Write-Host $result.Output
        $script:Fail++
        return
    }
    $script:Pass++
}

function Invoke-ErrorCase {
    param([string]$CuffFile)
    $expectedCodeFile = [System.IO.Path]::ChangeExtension($CuffFile, '.expected_code')
    $argsFile = [System.IO.Path]::ChangeExtension($CuffFile, '.args')
    $extraArgs = @()
    if (Test-Path $argsFile) {
        $extraArgs = (Get-Content -Raw $argsFile).Trim() -split '\s+' | Where-Object { $_ -ne '' }
    }
    $result = Invoke-WithTimeout -FilePath $Bin -Arguments ($extraArgs + @($CuffFile))
    if ($result.ExitCode -eq 0) {
        Write-Host "FAIL (expected nonzero exit): $CuffFile"
        $script:Fail++
        return
    }
    if (Test-Path $expectedCodeFile) {
        $expectedCode = (Get-Content -Raw $expectedCodeFile).Trim()
        if ($result.Output -notmatch [regex]::Escape("[$expectedCode]")) {
            Write-Host "FAIL (expected $expectedCode not found): $CuffFile"
            Write-Host $result.Output
            $script:Fail++
            return
        }
    }
    $script:Pass++
}

Test-Binary

Write-Host "== tests/unit (C++ unit tests) =="
Get-ChildItem 'tests\unit\*.cpp' | ForEach-Object {
    $src = $_.FullName
    $unitBin = Join-Path $env:TEMP "cuff_unit_$($_.BaseName).exe"
    $buildLog = Join-Path $env:TEMP 'cuff_unit_build.log'
    & g++ -std=c++17 -Wall -Wextra -O2 -I. $src -o $unitBin 2> $buildLog
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL (build): $($_.Name)"
        Get-Content $buildLog | Write-Host
        $Fail++
        return
    }
    $result = Invoke-WithTimeout -FilePath $unitBin -Arguments @() -TimeoutSeconds 60
    if ($result.ExitCode -eq 0) {
        $lastLine = ($result.Output -split "`n")[-1]
        Write-Host "  $($_.Name): $lastLine"
        $Pass++
    }
    else {
        Write-Host "FAIL: $($_.Name)"
        Write-Host $result.Output
        $Fail++
    }
}

Write-Host "== tests/cases (output diff) =="
Get-ChildItem 'tests\cases\*.cuff' | ForEach-Object { Invoke-SuccessCase $_.FullName }

Write-Host "== tests/errors (error code check) =="
Get-ChildItem 'tests\errors\*.cuff' | ForEach-Object { Invoke-ErrorCase $_.FullName }

Write-Host "== examples (output diff) =="
Get-ChildItem 'examples\*.cuff' | Where-Object { $_.Name -match '^[01]' } | Sort-Object Name |
    ForEach-Object { Invoke-SuccessCase $_.FullName }

Write-Host "== examples/error_cases (error code check) =="
Get-ChildItem 'examples\error_cases\*.cuff' | ForEach-Object { Invoke-ErrorCase $_.FullName }

Write-Host ""
Write-Host "$Pass passed, $Fail failed"
if ($Fail -eq 0) { exit 0 } else { exit 1 }
