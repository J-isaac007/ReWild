<#
.SYNOPSIS
  Phase 1/2: surveys the NSO modules of every extracted game build.
.DESCRIPTION
  Run 01-identify.ps1 first. For each extracted ExeFS (one per build, e.g.
  1.0.0, 1.5.0, 1.9.0) and each NSO in it (rtld, main, subsdk*, sdk):
    - decompresses it with hactool into extracted\nso\<build>\ (load these in
      Ghidra with the Switch Loader)
    - reports segment sizes, build ID and module path
    - finds SDK version markers and NVN entry-point strings
  Output: research\nso-survey.json (git-ignored).
#>
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Hactool,
    [string]$Keys
)

$ErrorActionPreference = 'Stop'
if (-not $Hactool) { $Hactool = Join-Path $Root 'tools\bin\hactool.exe' }
if (-not $Keys) {
    # Filtered key file written by 01-identify.ps1 (hactool rejects some newer key entries).
    $Keys = Join-Path $Root 'keys\prod.hactool.keys'
    if (-not (Test-Path $Keys)) { $Keys = Join-Path $Root 'keys\prod.keys' }
}
$extracted = Join-Path $Root 'extracted'

function Fail([string]$Message) { Write-Host "ERROR: $Message" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $Hactool)) { Fail "hactool not found at $Hactool (see docs\TOOLS.md)" }

# 01-identify.ps1 writes extracted\<kind>\<package>\exefs\<programid>_sdk<version>\
$exefsDirs = @()
foreach ($kind in 'update', 'base') {
    $dir = Join-Path $extracted $kind
    if (Test-Path $dir) { $exefsDirs += @(Get-ChildItem $dir -Recurse -Directory | Where-Object { $_.Parent.Name -eq 'exefs' -and (Test-Path (Join-Path $_.FullName 'main')) }) }
}
if ($exefsDirs.Count -eq 0) { Fail 'no extracted ExeFS with a main module; run scripts\diag\01-identify.ps1 first' }

# Map each build label to its display version via identity.json, when available.
$versionOf = @{}
$identityPath = Join-Path $Root 'research\identity.json'
if (Test-Path $identityPath) {
    $identity = Get-Content $identityPath -Raw | ConvertFrom-Json
    foreach ($p in $identity.Packages) { foreach ($n in $p.Nacps) { if ($n.Label) { $versionOf[$n.Label] = $n.DisplayVersion } } }
}

$markers = 'SDK MW\+[ -~]+', 'nnSdk-[ -~]+', 'NintendoSdk_[ -~]+'
$nvnNames = 'nvnBootstrapLoader', 'nvnDeviceInitialize', 'nvnDeviceGetProcAddress', 'nvnQueueSubmitCommands'

$builds = @()
foreach ($src in $exefsDirs) {
    $label = $src.Name
    $version = $versionOf[$label]
    ''
    "== $label$(if ($version) { " (v$version)" })"
    $outDir = Join-Path $extracted "nso\$label"
    New-Item -ItemType Directory -Force $outDir | Out-Null

    $modules = @()
    foreach ($f in Get-ChildItem $src.FullName -File | Where-Object { $_.Name -ne 'main.npdm' }) {
        $head = [IO.File]::ReadAllBytes($f.FullName)
        if ($head.Length -lt 0x100 -or [Text.Encoding]::ASCII.GetString($head, 0, 4) -ne 'NSO0') { continue }

        $plain = Join-Path $outDir "$($f.Name).nso"
        $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        $log = & $Hactool -k $Keys --disablekeywarns -t nso "--uncompressed=$plain" $f.FullName 2>&1 | ForEach-Object { "$_" }
        $ErrorActionPreference = $old
        if (-not (Test-Path $plain)) { Write-Host "  hactool could not decompress $($f.Name):`n$($log -join "`n")" -ForegroundColor Yellow; continue }

        $b = [IO.File]::ReadAllBytes($plain)
        $seg = { param($off) [ordered]@{ FileOffset = [BitConverter]::ToUInt32($b, $off); MemoryOffset = [BitConverter]::ToUInt32($b, $off + 4); Size = [BitConverter]::ToUInt32($b, $off + 8) } }
        $text = & $seg 0x10; $ro = & $seg 0x20; $data = & $seg 0x30

        # Read-only data starts with the module info: u32 0, u32 path length, path.
        $roBytes = New-Object byte[] $ro.Size
        [Array]::Copy($b, $ro.FileOffset, $roBytes, 0, [Math]::Min($ro.Size, $b.Length - $ro.FileOffset))
        $modulePath = $null
        if ([BitConverter]::ToUInt32($roBytes, 0) -eq 0) {
            $len = [BitConverter]::ToUInt32($roBytes, 4)
            if ($len -gt 0 -and $len -lt 0x200) { $modulePath = [Text.Encoding]::ASCII.GetString($roBytes, 8, $len).TrimEnd([char]0) }
        }
        $roText = [Text.Encoding]::ASCII.GetString($roBytes)

        $found = @()
        foreach ($m in $markers) { $found += @([regex]::Matches($roText, $m) | ForEach-Object { $_.Value } | Select-Object -Unique) }
        $nvn = @($nvnNames | Where-Object { $roText.Contains($_) })

        $modules += [ordered]@{
            Module       = $f.Name
            BuildId      = (-join ($b[0x40..0x5F] | ForEach-Object { $_.ToString('x2') })).TrimEnd('0')
            ModulePath   = $modulePath
            Text         = $text
            Ro           = $ro
            Data         = $data
            BssSize      = [BitConverter]::ToUInt32($b, 0x3C)
            SdkMarkers   = $found
            NvnStrings   = $nvn
            Uncompressed = $plain
        }
        "{0,-8} text {1,12:N0}  ro {2,11:N0}  data {3,10:N0}  build {4}" -f $f.Name, $text.Size, $ro.Size, $data.Size, $modules[-1].BuildId
        if ($modulePath) { "         module $modulePath" }
        foreach ($s in $found | Select-Object -First 3) { "         sdk    $s" }
        if ($nvn) { "         nvn    $($nvn -join ', ')" }
    }
    $builds += [ordered]@{ Label = $label; Version = $version; ExeFs = $src.FullName; Modules = $modules }
}

$jsonPath = Join-Path $Root 'research\nso-survey.json'
[ordered]@{ GeneratedAt = (Get-Date).ToString('s'); Builds = $builds } | ConvertTo-Json -Depth 7 | Set-Content -Path $jsonPath -Encoding UTF8
''
"Uncompressed NSOs for Ghidra: $(Join-Path $extracted 'nso')"
"Wrote $jsonPath"
