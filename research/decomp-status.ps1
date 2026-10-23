<#
.SYNOPSIS
  Measures zeldaret/botw decompilation progress from its function table.
.DESCRIPTION
  Downloads data/uking_functions.csv (cached in research/cache/) and prints
  function counts and code bytes per status. Status codes:
    O matching, m minor mismatch, M major mismatch, W work in progress,
    L library code, U not decompiled.
.PARAMETER Refresh
  Download the CSV again even if a cached copy exists.
#>
param([switch]$Refresh)

$ErrorActionPreference = 'Stop'
$url = 'https://raw.githubusercontent.com/zeldaret/botw/master/data/uking_functions.csv'
$cacheDir = Join-Path $PSScriptRoot 'cache'
$csvPath = Join-Path $cacheDir 'uking_functions.csv'

if ($Refresh -or -not (Test-Path $csvPath)) {
    New-Item -ItemType Directory -Force $cacheDir | Out-Null
    Invoke-WebRequest -Uri $url -OutFile $csvPath -UseBasicParsing
}

# Status codes differ only by case (m vs M), so use case-sensitive dictionaries.
$labels = New-Object 'System.Collections.Generic.Dictionary[string,string]' ([StringComparer]::Ordinal)
$labels['O'] = 'matching'; $labels['m'] = 'minor mismatch'; $labels['M'] = 'major mismatch'
$labels['W'] = 'work in progress'; $labels['L'] = 'library'; $labels['U'] = 'not decompiled'
$count = New-Object 'System.Collections.Generic.Dictionary[string,int64]' ([StringComparer]::Ordinal)
$bytes = New-Object 'System.Collections.Generic.Dictionary[string,int64]' ([StringComparer]::Ordinal)
$first = $null; $last = $null

foreach ($line in [IO.File]::ReadLines($csvPath)) {
    if (-not $line.StartsWith('0x')) { continue }
    $parts = $line.Split(',')
    $status = $parts[1]
    if (-not $count.ContainsKey($status)) { $count[$status] = 0; $bytes[$status] = 0 }
    $count[$status] += 1
    $bytes[$status] += [int64]$parts[2]
    if (-not $first) { $first = $parts[0] }
    $last = $parts[0]
}

$totalFns = ($count.Values | Measure-Object -Sum).Sum
$totalBytes = ($bytes.Values | Measure-Object -Sum).Sum

"Source:    $url"
"Cached:    $((Get-Item $csvPath).LastWriteTime)"
"Functions: {0:N0}" -f $totalFns
"Code:      {0:N0} bytes ({1:N1} MB), {2} .. {3}" -f $totalBytes, ($totalBytes / 1MB), $first, $last
''
$bytes.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object {
    [pscustomobject]@{
        Status    = $_.Key
        Meaning   = $labels[$_.Key]
        Functions = $count[$_.Key]
        Bytes     = $_.Value
        Share     = '{0:N1}%' -f (100 * $_.Value / $totalBytes)
    }
} | Format-Table -AutoSize
