<#
.SYNOPSIS
  B2 boot loop: starts port\build\botwrecomp.exe straight into the game, stops it after a while, and summarises the log.
.DESCRIPTION
  Runs from port\ (dev mode: extracted\ is there) with LARECOMP_NO_LAUNCHER=1 and MSYS2's UCRT64 DLLs on PATH. Prints the
  boot progress lines, every [hle] UNIMPLEMENTED, unhandled service commands, and, on a crash, the guest registers with
  the crash addresses named by port\tools\sym.py. The full log stays in port\build\boot-<n>.log (git-ignored).
.PARAMETER Seconds
  How long to let it run before stopping it.
.PARAMETER Env
  Extra environment variables for the run, e.g. @{LARECOMP_LOG_HEAPS='1'}.
#>
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [int]$Seconds = 45,
    [hashtable]$Env = @{}
)

$ErrorActionPreference = 'Stop'
$port = Join-Path $Root 'port'
$exe = Join-Path $port 'build\botwrecomp.exe'
if (-not (Test-Path $exe)) { Write-Host "ERROR: no $exe (make in port/)" -ForegroundColor Red; exit 1 }

$n = 1
while (Test-Path (Join-Path $port "build\boot-$n.log")) { $n++ }
$log = Join-Path $port "build\boot-$n.log"

$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$env:LARECOMP_NO_LAUNCHER = '1'
foreach ($k in $Env.Keys) { Set-Item "env:$k" $Env[$k] }
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe -ArgumentList '--play' -WorkingDirectory $port -RedirectStandardError $log `
    -RedirectStandardOutput "$log.out" -PassThru
$null = $p.Handle  # (keeps the process handle, so ExitCode is there after it exits)
$done = $p.WaitForExit($Seconds * 1000)
if (-not $done) { Stop-Process -Id $p.Id -Force; $how = "still running after $Seconds s (stopped)" }
else { $how = "exited after $([int]$sw.Elapsed.TotalSeconds) s (code 0x{0:x})" -f $p.ExitCode }

$lines = Get-Content $log
"=== boot-$n : $how, $($lines.Count) log lines"
'--- progress:'
$lines | Where-Object { $_ -match '^\[gamelib\]|^loaded |^init |^\[vi\]|^\[nvn|^\[vk\]|frame \d|present|^\[movie' } | Select-Object -First 25
'--- unimplemented / unhandled (first of each):'
$lines | Where-Object { $_ -match 'UNIMPLEMENTED|unhandled|unknown|not implemented' } |
    ForEach-Object { ($_ -replace '\(x0=.*$', '') -replace '\s+$', '' } | Select-Object -Unique | Select-Object -First 30
$crash = $lines | Where-Object { $_ -match '^\*\*\* |^FATAL' } | Select-Object -First 3
if ($crash) {
    '--- crash:'
    $crash
    $lines | Where-Object { $_ -match '^guest: |^  x\d' }
    # name the guest pc (from f_<addr> in the host pc line) and lr
    $addrs = @()
    foreach ($l in $lines) {
        if ($l -match 'f_([0-9a-f]{10})') { $addrs += "0x$($Matches[1])" }
        if ($l -match '^guest: .*lr=([0-9a-f]+)') { $addrs += "0x$($Matches[1])" }
        if ($l -match 'no function at ([0-9a-f]+) \(lr=([0-9a-f]+)') { $addrs += "0x$($Matches[1])"; $addrs += "0x$($Matches[2])" }
    }
    $addrs = @($addrs | Select-Object -Unique | Select-Object -First 6)  # (@: one address must stay an array)
    if ($addrs) {
        '--- named:'
        Push-Location (Join-Path $port 'tools')
        $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        & (Join-Path $port 'tools\venv\Scripts\python.exe') sym.py @addrs 2>&1 | ForEach-Object { "  $_" }
        $ErrorActionPreference = $old
        Pop-Location
    }
}
'--- last lines:'
$lines | Select-Object -Last 12
"(full log: $log)"
