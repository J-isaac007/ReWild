<#
.SYNOPSIS
  Phase 1: identifies your BotW dump and extracts what later phases need.
.DESCRIPTION
  Reads your own dump from the repo's git-ignored folders:
    keys\prod.keys     console keys (Lockpick_RCM)
    dumps\base\        base game .xci (or .nsp)
    dumps\update\      update .nsp
    dumps\dlc\         DLC .nsp files
    dumps\firmware\    firmware NCAs (optional)
  For each package it unpacks the NCAs with hactool, reads and extracts them
  with nstool (ExeFS: main, rtld, sdk, subsdk*; control data), and records title IDs,
  the game's display version (from control.nacp), the firmware version,
  and SHA-256 plus build ID for every NSO.
  Output: extracted\ (git-ignored) and research\identity.json (git-ignored).
.PARAMETER CheckOnly
  Only validate that the tools and inputs are present; extract nothing.
#>
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Hactool,
    [string]$Nstool,
    [string]$Keys,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
if (-not $Hactool) { $Hactool = Join-Path $Root 'tools\bin\hactool.exe' }
if (-not $Nstool) { $Nstool = Join-Path $Root 'tools\bin\nstool.exe' }
if (-not $Keys) { $Keys = Join-Path $Root 'keys\prod.keys' }
$dumps = Join-Path $Root 'dumps'
$out = Join-Path $Root 'extracted'

function Fail([string]$Message) { Write-Host "ERROR: $Message" -ForegroundColor Red; exit 1 }

function Get-Packages([string]$Dir, [string[]]$Patterns) {
    if (-not (Test-Path $Dir)) { return @() }
    @(Get-ChildItem $Dir -File | Where-Object { $n = $_.Name; $Patterns | Where-Object { $n -like $_ } })
}

# ---- Preflight -------------------------------------------------------------
$problems = @()
if (-not (Test-Path $Hactool)) { $problems += "hactool not found at $Hactool (see docs\TOOLS.md; put hactool.exe in tools\bin\)" }
if (-not (Test-Path $Nstool)) { $problems += "nstool not found at $Nstool (see docs\TOOLS.md; put nstool.exe in tools\bin\)" }
if (-not (Test-Path $Keys)) { $problems += "keys not found at $Keys (dump your own console's keys with Lockpick_RCM)" }
$base = Get-Packages (Join-Path $dumps 'base') '*.xci', '*.nsp'
$update = Get-Packages (Join-Path $dumps 'update') '*.nsp'
$dlc = Get-Packages (Join-Path $dumps 'dlc') '*.nsp'
$compressed = @()
if (Test-Path $dumps) { $compressed = @(Get-ChildItem $dumps -Recurse -File -Include '*.nsz', '*.xcz') }
if ($base.Count -eq 0) { $problems += "no base game in $dumps\base (expected your .xci)" }
if ($base.Count -gt 1) { $problems += "more than one file in $dumps\base; keep only the BotW base game" }
if ($compressed.Count -gt 0) { $problems += "compressed packages (.nsz/.xcz) aren't supported by hactool; dump uncompressed .nsp/.xci" }

"Root:     $Root"
"hactool:  $Hactool"
"Keys:     $Keys"
"Base:     $(($base | ForEach-Object Name) -join ', ')"
"Update:   $(($update | ForEach-Object Name) -join ', ')"
"DLC:      $(($dlc | ForEach-Object Name) -join ', ')"
if ($problems) { $problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }; Fail 'inputs incomplete' }
if ($CheckOnly) { 'Preflight OK.'; exit 0 }

# ---- hactool wrapper ---------------------------------------------------------
# hactool 1.4.0 aborts on any key whose length it doesn't expect (newer dumpers
# write some *_kek_source entries with an extra byte). It only needs the 16- and
# 32-byte keys, so hand it a filtered copy next to the original.
$HactoolKeys = Join-Path (Split-Path $Keys) 'prod.hactool.keys'
Get-Content $Keys | Where-Object { $_ -match '^\s*[A-Za-z0-9_]+\s*=\s*([0-9A-Fa-f]{32}|[0-9A-Fa-f]{64})\s*$' } | Set-Content -Path $HactoolKeys -Encoding ASCII

function Invoke-Hactool([string[]]$Arguments) {
    # Windows PowerShell turns native stderr into error records; collect it as text instead.
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $lines = & $Hactool -k $HactoolKeys --disablekeywarns @Arguments 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -notmatch 'prod\.keys does not exist' }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    [pscustomobject]@{ Code = $code; Lines = $lines; Text = ($lines -join "`n") }
}

function Get-Field([string]$Text, [string]$Label) {
    if ($Text -match "(?m)^\s*$([regex]::Escape($Label)):\s*(.+?)\s*$") { return $Matches[1] }
    $null
}

# ---- Tickets: rights ID -> encrypted title key (common tickets only) ---------
$titleKeys = @{}
function Read-Ticket([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 0x2C0) { return }
    $type = $b[0x281]                     # 0 = common, 1 = personalized
    $rights = -join ($b[0x2A0..0x2AF] | ForEach-Object { $_.ToString('x2') })
    $key = -join ($b[0x180..0x18F] | ForEach-Object { $_.ToString('x2') })
    if ($type -eq 0) { $titleKeys[$rights] = $key }
    else { Write-Host "  ticket $([IO.Path]::GetFileName($Path)) is personalized; re-dump with nxdumptool's 'remove console specific data' option" -ForegroundColor Yellow }
}

# ---- NCA processing (nstool: hactool 1.4.0 can't parse 2026-era patch NCAs) ----
function Invoke-Nstool([string[]]$Arguments) {
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $lines = & $Nstool -k $HactoolKeys @Arguments 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -notmatch 'title\.keys" keyfile' }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    [pscustomobject]@{ Code = $code; Lines = $lines; Text = ($lines -join "`n") }
}

function Get-NcaInfo([string]$Path) {
    $r = Invoke-Nstool @('-t', 'nca', $Path)
    $rights = Get-Field $r.Text 'RightsId'
    if ($rights) { $rights = $rights.ToLower() }
    $tid = Get-Field $r.Text 'ProgID'
    if ($tid) { $tid = $tid -replace '^0x', '' }
    $sdk = Get-Field $r.Text 'SdkAddon Ver.'
    if ($sdk) { $sdk = ($sdk -split ' ')[0] }
    [pscustomobject]@{
        Path        = $Path
        Name        = [IO.Path]::GetFileName($Path)
        ContentType = Get-Field $r.Text 'Content Type'
        TitleId     = $tid
        SdkVersion  = $sdk
        KeyGen      = Get-Field $r.Text 'Key Generation'
        RightsId    = $rights
        Ok          = ($r.Code -eq 0)
        Raw         = $r.Text
    }
}

# Extracts partition 0: ExeFS for Program NCAs, RomFS for Control NCAs.
function Expand-Nca($Nca, [string]$OutDir) {
    $extra = @()
    if ($Nca.RightsId) {
        if (-not $titleKeys.ContainsKey($Nca.RightsId)) { Write-Host "  no title key for $($Nca.Name) (rights ID $($Nca.RightsId))" -ForegroundColor Yellow; return $false }
        $extra = @('--titlekey', $titleKeys[$Nca.RightsId])
    }
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $r = Invoke-Nstool ($extra + @('-x', '/0', $OutDir, $Nca.Path))
    if ($r.Code -ne 0 -or -not (Get-ChildItem $OutDir -Recurse -File)) { Write-Host "  nstool failed on $($Nca.Name):`n$($r.Text)" -ForegroundColor Yellow; return $false }
    $true
}

function Expand-Deflate([byte[]]$Data, [int]$Offset, [int]$Length) {
    $in = [IO.MemoryStream]::new($Data, $Offset, $Length)
    $z = [IO.Compression.DeflateStream]::new($in, [IO.Compression.CompressionMode]::Decompress)
    $outStream = [IO.MemoryStream]::new()
    $z.CopyTo($outStream); $z.Dispose()
    , $outStream.ToArray()
}

function Read-Nacp([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    $titles = $b
    $format = 'plain'
    # Newer NACPs (seen in BotW v1.9.0) compress the title block (0x0-0x3000):
    # u16 compressed length, then a raw deflate stream. The rest stays plain.
    $len = [BitConverter]::ToUInt16($b, 0)
    if ($len -gt 0 -and $len -lt 0x2FFE -and $b[2] -ge 0x80) {
        try { $titles = Expand-Deflate $b 2 $len; $format = 'compressed titles' } catch { $titles = $b }
    }
    $str = { param($buf, $off, $n) $s = [Text.Encoding]::UTF8.GetString($buf, $off, $n); $s.Substring(0, [Math]::Max(0, $s.IndexOf([char]0))) }
    [ordered]@{ Name = & $str $titles 0x0 0x200; Publisher = & $str $titles 0x200 0x100; DisplayVersion = & $str $b 0x3060 0x10; Format = $format }
}

function Read-Nso([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 0x100 -or [Text.Encoding]::ASCII.GetString($b, 0, 4) -ne 'NSO0') { return $null }
    $seg = { param($off) [ordered]@{ FileOffset = [BitConverter]::ToUInt32($b, $off); MemoryOffset = [BitConverter]::ToUInt32($b, $off + 4); Size = [BitConverter]::ToUInt32($b, $off + 8) } }
    $flags = [BitConverter]::ToUInt32($b, 0xC)
    [ordered]@{
        BuildId    = (-join ($b[0x40..0x5F] | ForEach-Object { $_.ToString('x2') })).TrimEnd('0')
        Text       = & $seg 0x10
        Ro         = & $seg 0x20
        Data       = & $seg 0x30
        BssSize    = [BitConverter]::ToUInt32($b, 0x3C)
        Compressed = [ordered]@{ Text = [bool]($flags -band 1); Ro = [bool]($flags -band 2); Data = [bool]($flags -band 4) }
    }
}

function Process-Package($File, [string]$Kind) {
    $dir = Join-Path $out "$Kind\$([IO.Path]::GetFileNameWithoutExtension($File.Name))"
    $raw = Join-Path $dir 'raw'
    New-Item -ItemType Directory -Force $raw | Out-Null
    "[$Kind] $($File.Name)"
    $type = if ($File.Extension -eq '.xci') { 'xci' } else { 'pfs0' }
    $dirArg = if ($type -eq 'xci') { "--securedir=$raw" } else { "--outdir=$raw" }
    # Unpacking a cartridge copies ~15 GB, so reuse an earlier unpack.
    if (-not (Get-ChildItem $raw -Filter '*.nca' -ErrorAction SilentlyContinue)) {
        $r = Invoke-Hactool @('-t', $type, $dirArg, $File.FullName)
        if ($r.Code -ne 0) { Write-Host $r.Text -ForegroundColor Yellow; Fail "could not unpack $($File.Name)" }
    }

    Get-ChildItem $raw -Filter '*.tik' | ForEach-Object { Read-Ticket $_.FullName }
    $ncas = @(Get-ChildItem $raw -Filter '*.nca' | ForEach-Object { Get-NcaInfo $_.FullName })

    # A cartridge can also carry a bundled update with the same program ID, so
    # output folders are named by program ID plus SDK version (one per build).
    $pkg = [ordered]@{ File = $File.Name; Kind = $Kind; Sha256 = (Get-FileHash $File.FullName -Algorithm SHA256).Hash.ToLower(); Ncas = @(); Nacps = @(); ExeFs = @() }
    foreach ($n in $ncas) {
        $pkg.Ncas += [ordered]@{ Name = $n.Name; ContentType = $n.ContentType; TitleId = $n.TitleId; SdkVersion = $n.SdkVersion; KeyGeneration = $n.KeyGen; TitleKeyCrypto = [bool]$n.RightsId }
        $tid = if ($n.TitleId) { $n.TitleId.ToLower() } else { [IO.Path]::GetFileNameWithoutExtension($n.Name) }
        $label = "${tid}_sdk$($n.SdkVersion)"
        switch ($n.ContentType) {
            'Control' {
                $cdir = Join-Path $dir "control\$label"
                if (Expand-Nca $n $cdir) {
                    $nacp = Join-Path $cdir 'control.nacp'
                    if (Test-Path $nacp) { $info = Read-Nacp $nacp; $info.TitleId = $tid; $info.Label = $label; $pkg.Nacps += $info }
                }
            }
            'Program' {
                $edir = Join-Path $dir "exefs\$label"
                if (Expand-Nca $n $edir) {
                    foreach ($f in Get-ChildItem $edir -File) {
                        $entry = [ordered]@{ TitleId = $tid; Label = $label; Name = $f.Name; Size = $f.Length; Sha256 = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower() }
                        $nso = Read-Nso $f.FullName
                        if ($nso) { $entry.Nso = $nso }
                        $pkg.ExeFs += $entry
                    }
                }
            }
        }
    }
    $pkg
}

# ---- Run -------------------------------------------------------------------
New-Item -ItemType Directory -Force $out | Out-Null
$packages = @()
# Tickets can live in any package, so unpack update and DLC before resolving NCAs that need them.
foreach ($f in $update) { $packages += Process-Package $f 'update' }
foreach ($f in $dlc) { $packages += Process-Package $f 'dlc' }
foreach ($f in $base) { $packages += Process-Package $f 'base' }

# ---- Firmware version (SystemVersion title 0100000000000809) ------------------
$firmware = $null
$fwDir = Join-Path $dumps 'firmware'
if (Test-Path $fwDir) {
    '[firmware] scanning NCAs for the SystemVersion title'
    foreach ($f in Get-ChildItem $fwDir -Recurse -File -Filter '*.nca') {
        $n = Get-NcaInfo $f.FullName
        # The title also has a Meta (.cnmt) NCA; the version file is in the Data one.
        if ($n.TitleId -and $n.TitleId.ToLower().TrimStart('0') -eq '100000000000809' -and $n.ContentType -eq 'Data') {
            $sdir = Join-Path $out 'firmware\systemversion'
            if (Expand-Nca $n $sdir) {
                $b = [IO.File]::ReadAllBytes((Join-Path $sdir 'file'))
                $str = { param($off, $len) $s = [Text.Encoding]::ASCII.GetString($b, $off, $len); $s.Substring(0, [Math]::Max(0, $s.IndexOf([char]0))) }
                $firmware = [ordered]@{ Version = "$($b[0]).$($b[1]).$($b[2])"; Display = & $str 0x68 0x18; Platform = & $str 0x8 0x20 }
            }
            break
        }
    }
    if (-not $firmware) { Write-Host '  SystemVersion title not found or not decryptable (firmware is optional)' -ForegroundColor Yellow }
}

$identity = [ordered]@{ GeneratedAt = (Get-Date).ToString('s'); Firmware = $firmware; Packages = $packages }
$jsonPath = Join-Path $Root 'research\identity.json'
$identity | ConvertTo-Json -Depth 8 | Set-Content -Path $jsonPath -Encoding UTF8

# ---- Summary -----------------------------------------------------------------
''
'===== Summary ====='
foreach ($p in $packages) {
    "{0,-7} {1}" -f $p.Kind, $p.File
    foreach ($a in $p.Nacps) { "          {0,-36} version {1,-8} {2}" -f $a.Label, $a.DisplayVersion, $a.Name }
    if (-not $p.Nacps) { foreach ($t in $p.Ncas | ForEach-Object { $_.TitleId } | Select-Object -Unique) { "          $t" } }
    foreach ($e in $p.ExeFs | Where-Object { $_.Nso }) { "          {0,-36} {1,-9} build {2}  text {3:N0} B" -f $e.Label, $e.Name, $e.Nso.BuildId, $e.Nso.Text.Size }
}
if ($firmware) { "firmware {0} ({1})" -f $firmware.Display, $firmware.Platform }
''
$versions = @($packages | Where-Object { $_.Kind -ne 'dlc' } | ForEach-Object { $_.Nacps } | ForEach-Object { $_.DisplayVersion } |
    Where-Object { $_ } | Sort-Object { try { [version]$_ } catch { [version]'0.0' } } -Unique)
"Game versions present: $($versions -join ', ') (highest runs)"
if ($versions -contains '1.5.0') { 'v1.5.0 code is present: the decomp symbols apply to it directly. Use it for P2.' }
$latest = $versions | Select-Object -Last 1
if ($latest -and $latest -ne '1.5.0') { "v$latest needs decomp symbols ported from 1.5.0 (Ghidra Version Tracking / BinDiff)." }
"Wrote $jsonPath"
