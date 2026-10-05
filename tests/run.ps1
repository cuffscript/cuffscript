# PowerShell equivalent of tests/run.sh. Works on Windows PowerShell 5.1 and
# PowerShell 7+ (Windows, Linux, macOS).
#
#   powershell -File tests\run.ps1

Set-StrictMode -Off
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..')

$OnWindows = ($env:OS -eq 'Windows_NT')
$Bin = if ($OnWindows) { '.\cuffc.exe' } else { './cuffc' }
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$script:Pass = 0
$script:Fail = 0

function ConvertTo-ArgString {
    param([string[]]$Items)
    $quoted = foreach ($item in $Items) {
        if ($item.Length -eq 0) { '""' }
        elseif ($item -match '[\s"]') {
            $escaped = $item -replace '(\\*)"', '$1$1\"'
            $escaped = $escaped -replace '(\\+)$', '$1$1'
            '"' + $escaped + '"'
        }
        else { $item }
    }
    return ($quoted -join ' ')
}

# Runs a program with a timeout. Output is decoded as UTF-8 (cuffc prints UTF-8
# regardless of the console code page), stdin is closed so input() sees EOF, and
# line endings are normalized to LF so results match the .expected files.
function Invoke-Program {
    param(
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [int]$TimeoutSeconds = 10
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = ConvertTo-ArgString $Arguments
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = $Utf8
    $psi.StandardErrorEncoding = $Utf8

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        $null = $proc.Start()
    }
    catch {
        return [PSCustomObject]@{ Output = "could not start ${FilePath}: $($_.Exception.Message)"; ExitCode = 127 }
    }
    $proc.StandardInput.Close()
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        try { $proc.Kill() } catch { }
        return [PSCustomObject]@{ Output = "(timed out after ${TimeoutSeconds}s)"; ExitCode = 124 }
    }
    $proc.WaitForExit()
    $text = ($stdoutTask.Result + $stderrTask.Result) -replace "`r`n", "`n"
    return [PSCustomObject]@{ Output = $text.TrimEnd("`n"); ExitCode = $proc.ExitCode }
}

function Read-Text {
    param([string]$Path)
    return ([System.IO.File]::ReadAllText((Resolve-Path $Path).Path, $Utf8) -replace "`r`n", "`n").TrimEnd("`n")
}

function Invoke-SuccessCase {
    param([string]$CuffFile)
    $expected = [System.IO.Path]::ChangeExtension($CuffFile, '.expected')
    $r = Invoke-Program -FilePath $Bin -Arguments @($CuffFile)
    if ($r.ExitCode -ne 0) {
        Write-Host "FAIL (exit $($r.ExitCode)): $CuffFile"
        Write-Host $r.Output
        $script:Fail++
        return
    }
    if (Test-Path $expected) {
        $want = Read-Text $expected
        if ($r.Output -ne $want) {
            Write-Host "FAIL (output mismatch): $CuffFile"
            Write-Host "--- expected ---"
            Write-Host $want
            Write-Host "--- actual ---"
            Write-Host $r.Output
            $script:Fail++
            return
        }
    }
    $script:Pass++
}

function Invoke-ErrorCase {
    param([string]$CuffFile)
    $codeFile = [System.IO.Path]::ChangeExtension($CuffFile, '.expected_code')
    $argsFile = [System.IO.Path]::ChangeExtension($CuffFile, '.args')
    $extra = @()
    if (Test-Path $argsFile) {
        $extra = @((Read-Text $argsFile) -split '\s+' | Where-Object { $_ -ne '' })
    }
    $r = Invoke-Program -FilePath $Bin -Arguments ($extra + @($CuffFile))
    if ($r.ExitCode -eq 0) {
        Write-Host "FAIL (expected nonzero exit): $CuffFile"
        $script:Fail++
        return
    }
    if (Test-Path $codeFile) {
        $code = (Read-Text $codeFile).Trim()
        if (-not $r.Output.Contains("[$code]")) {
            Write-Host "FAIL (expected $code not found): $CuffFile"
            Write-Host $r.Output
            $script:Fail++
            return
        }
    }
    $script:Pass++
}

if (-not (Test-Path $Bin)) {
    Write-Host "$Bin not found - build first (Windows: mingw32-make -f Makefile.win)"
    exit 1
}

Write-Host '== tests/unit (C++ unit tests) =='
$tmp = [System.IO.Path]::GetTempPath()
$exeSuffix = if ($OnWindows) { '.exe' } else { '' }
$linkArgs = if ($OnWindows) { @('-Wl,--stack,8388608', '-lws2_32') } else { @() }
foreach ($src in (Get-ChildItem 'tests/unit/*.cpp' | Sort-Object Name)) {
    $unitBin = Join-Path $tmp ("cuff_unit_" + $src.BaseName + $exeSuffix)
    $build = Invoke-Program -FilePath 'g++' -TimeoutSeconds 600 -Arguments (@('-std=c++17', '-Wall', '-Wextra', '-O2', '-I.', $src.FullName, '-o', $unitBin) + $linkArgs)
    if ($build.ExitCode -ne 0) {
        Write-Host "FAIL (build): $($src.Name)"
        Write-Host $build.Output
        $script:Fail++
        continue
    }
    $run = Invoke-Program -FilePath $unitBin -TimeoutSeconds 120
    if ($run.ExitCode -eq 0) {
        $last = ($run.Output -split "`n")[-1]
        Write-Host "  $($src.Name): $last"
        $script:Pass++
    }
    else {
        Write-Host "FAIL: $($src.Name)"
        Write-Host $run.Output
        $script:Fail++
    }
}

Write-Host '== tests/cases (output diff) =='
foreach ($f in (Get-ChildItem 'tests/cases/*.cuff' | Sort-Object Name)) { Invoke-SuccessCase $f.FullName }

Write-Host '== tests/errors (error code check) =='
foreach ($f in (Get-ChildItem 'tests/errors/*.cuff' | Sort-Object Name)) { Invoke-ErrorCase $f.FullName }

Write-Host '== examples (output diff) =='
foreach ($f in (Get-ChildItem 'examples/*.cuff' | Where-Object { $_.Name -match '^[01]' } | Sort-Object Name)) { Invoke-SuccessCase $f.FullName }

Write-Host '== examples/error_cases (error code check) =='
foreach ($f in (Get-ChildItem 'examples/error_cases/*.cuff' | Sort-Object Name)) { Invoke-ErrorCase $f.FullName }

Write-Host ''
Write-Host "$($script:Pass) passed, $($script:Fail) failed"
if ($script:Fail -eq 0) { exit 0 } else { exit 1 }
