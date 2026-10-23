<#
.SYNOPSIS
  Names BotW v1.5.0 main addresses with the zeldaret/botw decomp's function table (research\cache\uking_functions.csv).
.DESCRIPTION
  Takes runtime addresses (main at 0x7000000000, port/runtime/game_info.h) or decomp addresses (0x71...), and prints the
  decomp function containing each, its offset, status and demangled-ish name. Runs research\decomp-status.ps1 once
  first if the CSV isn't cached.
.EXAMPLE
  scripts\dev\decomp-name.ps1 0x7000b345c4 0x700001d1c0
#>
param([Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Address)

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$csv = Join-Path $root 'research\cache\uking_functions.csv'
if (-not (Test-Path $csv)) { & (Join-Path $root 'research\decomp-status.ps1') | Out-Null }

$rows = New-Object 'System.Collections.Generic.List[object]'
foreach ($line in [IO.File]::ReadLines($csv)) {
    if (-not $line.StartsWith('0x')) { continue }
    $p = $line.Split(',', 4)
    $rows.Add([pscustomobject]@{ Addr = [Convert]::ToUInt64($p[0].Substring(2), 16); Status = $p[1]; Size = [uint64]$p[2]; Name = $p[3] })
}
$addrs = $rows | ForEach-Object Addr
$sorted = [uint64[]]$addrs

foreach ($a in $Address) {
    $v = [Convert]::ToUInt64(($a -replace '^0x', ''), 16)
    if ($v -lt 0x7100000000) { $v += 0x100000000 }  # runtime main base 0x7000000000 -> decomp 0x7100000000
    $i = [Array]::BinarySearch($sorted, $v)
    if ($i -lt 0) { $i = (-bnot $i) - 1 }
    if ($i -lt 0) { "{0}: before the first function" -f $a; continue }
    $r = $rows[$i]
    $off = $v - $r.Addr
    $inside = if ($off -lt $r.Size) { '' } else { ' (past its end: between functions?)' }
    "{0} = decomp 0x{1:x} -> 0x{2:x}+0x{3:x} [{4}] {5}{6}" -f $a, $v, $r.Addr, $off, $r.Status, $(if ($r.Name) { $r.Name } else { '(unnamed)' }), $inside
}
