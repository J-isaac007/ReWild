<#
.SYNOPSIS
  Prints Ghidra's decompiled C for the functions containing the given addresses (v1.5.0 main, reWild's Ghidra project).
.DESCRIPTION
  Runtime (0x70...) or decomp (0x71...) addresses. Runs Ghidra headless read-only against ghidra\reWild.gpr (made by
  import-main.ps1) with the headless settings folder (see docs\TOOLS.md). Close the project in the Ghidra GUI first.
.EXAMPLE
  scripts\ghidra\decompile.ps1 0x7000b3456c 0x7000b345c4
#>
param([Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Address)

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:APPDATA = 'C:\tools\ghidra-headless-appdata'
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-25.0.4'
$env:GHIDRA_HEADLESS_MAXMEM = '8G'
$old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
$out = & 'C:\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat' "$root\ghidra" reWild -process main.nso -noanalysis `
    -readOnly -scriptPath "$root\scripts\ghidra" -postScript Decompile.java @Address 2>&1 | ForEach-Object { "$_" }
$ErrorActionPreference = $old
# keep the script's output: from each DECOMPILE header to the next log line
$on = $false
foreach ($line in $out) {
    if ($line -match 'Decompile\.java> (DECOMPILE .*)') { $on = $true; $Matches[1] -replace ' \(GhidraScript\)\s*$', ''; continue }
    if ($line -match '^\s*(INFO|WARN|ERROR)\s') { $on = $false; continue }
    if ($on) { $line }
}
