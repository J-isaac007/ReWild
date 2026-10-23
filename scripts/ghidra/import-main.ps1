<#
.SYNOPSIS
  Phase 2: imports a BotW main.nso into the reWild Ghidra project with decomp names.
.DESCRIPTION
  Runs Ghidra headless, detached from the calling shell so it survives the
  terminal closing:
    1. imports the NSO with the Switch Loader
    2. pre-script: decomp names as labels + entry points
    3. full auto-analysis (creates functions, demangles names); can take hours
    4. post-script: missing functions, decomp status tags
  Progress goes to ghidra\import-<build>.log; Ghidra's own log is in
  C:\tools\ghidra-headless-appdata\ghidra\ghidra_12.1.4_PUBLIC\application.log.

  Headless runs use their own settings folder (APPDATA) because Ghidra 12.1.4's
  OSGi framework (Felix) fails to start scripts from the default one under JDK 25.
  The Switch Loader is copied there.
.PARAMETER Build
  Folder name under extracted\nso\ (default: the v1.5.0 build).
.PARAMETER Wait
  Run in the foreground instead of detaching.
#>
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Build = '01007ef00011e000_sdk4.4.0',
    [string]$Ghidra = 'C:\tools\ghidra_12.1.4_PUBLIC',
    [string]$Jdk = 'C:\Program Files\Java\jdk-25.0.4',
    [string]$MaxMem = '8G',
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
$nso = Join-Path $Root "extracted\nso\$Build\main.nso"
$csv = Join-Path $Root 'research\cache\uking_functions.csv'
$project = Join-Path $Root 'ghidra'
$headlessHome = 'C:\tools\ghidra-headless-appdata'
$log = Join-Path $project "import-$Build.log"

foreach ($p in $nso, $csv, "$Ghidra\support\analyzeHeadless.bat") {
    if (-not (Test-Path $p)) { Write-Host "ERROR: missing $p" -ForegroundColor Red; exit 1 }
}
$ext = Join-Path $headlessHome 'ghidra\ghidra_12.1.4_PUBLIC\Extensions'
if (-not (Test-Path (Join-Path $ext 'SwitchLoader'))) {
    New-Item -ItemType Directory -Force $ext | Out-Null
    Copy-Item "$env:APPDATA\ghidra\ghidra_12.1.4_PUBLIC\Extensions\SwitchLoader" $ext -Recurse
}
New-Item -ItemType Directory -Force $project | Out-Null

# Quoted set: cmd would otherwise keep the space before && in the value.
$cmd = @(
    "set `"APPDATA=$headlessHome`""
    "set `"JAVA_HOME=$Jdk`""
    "set `"GHIDRA_HEADLESS_MAXMEM=$MaxMem`""
    "`"$Ghidra\support\analyzeHeadless.bat`" `"$project`" reWild -import `"$nso`" -overwrite " +
        "-scriptPath `"$Root\scripts\ghidra`" " +
        "-preScript ApplyDecompNames.java `"$csv`" labels " +
        "-postScript ApplyDecompNames.java `"$csv`" functions " +
        "> `"$log`" 2>&1"
) -join ' && '

if ($Wait) {
    cmd /c $cmd
    Get-Content $log -Tail 20
}
else {
    $p = Start-Process cmd -ArgumentList '/c', $cmd -WindowStyle Hidden -PassThru
    "Started Ghidra headless (cmd PID $($p.Id)). Log: $log"
}
