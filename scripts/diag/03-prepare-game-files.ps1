<#
.SYNOPSIS
  B1: puts one game build's files where the port's runtime reads them (port\extracted\).
.DESCRIPTION
  Run 01-identify.ps1 first (it unpacks the NCAs). For the chosen build (default: v1.5.0, the update inside the BotW
  cartridge image, which the decomp matches):
    exefs\     main, sdk, subsdk0, rtld, main.npdm of that build
    romfs\     the base game's RomFS with the update's patch (BKTR) applied, extracted by hactool (about 14 GB;
               hactool writes a patched RomFS only as a folder, not as an image)
    romfs.bin  the RomFS image the runtime reads (runtime/services/fs.c), built from romfs\ by port's
               build\romfs_image (`make romfs-image`: the start screen's own builder, runtime/romfs_build.c)
  Output is git-ignored (port\.gitignore: extracted/).
.PARAMETER Build
  The exefs folder label from 01-identify (<program id>_sdk<version>).
.PARAMETER Force
  Rewrite files that already exist.
#>
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Build = '01007ef00011e000_sdk4.4.0',
    [string]$Out,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
if (-not $Out) { $Out = Join-Path $Root 'port\extracted' }
$Hactool = Join-Path $Root 'tools\bin\hactool.exe'
$Nstool = Join-Path $Root 'tools\bin\nstool.exe'
$Keys = Join-Path $Root 'keys\prod.hactool.keys'  # (written by 01-identify: the entries hactool accepts)
$RomfsImage = Join-Path $Root 'port\build\romfs_image.exe'
$extracted = Join-Path $Root 'extracted'

function Fail([string]$Message) { Write-Host "ERROR: $Message" -ForegroundColor Red; exit 1 }
foreach ($p in $Hactool, $Nstool, $Keys) { if (-not (Test-Path $p)) { Fail "missing $p (run scripts\diag\01-identify.ps1 first)" } }
if (-not (Test-Path $RomfsImage)) { Fail "missing $RomfsImage (in port/, MSYS2 UCRT64 shell: make romfs-image)" }

function Invoke-Tool([string]$Exe, [string[]]$Arguments) {
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $lines = & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    [pscustomobject]@{ Code = $code; Text = ($lines -join "`n") }
}

function Get-NcaHeader([string]$Path) {
    $t = (Invoke-Tool $Nstool @('-k', $Keys, '-t', 'nca', $Path)).Text
    $get = { param($label) if ($t -match "(?m)^\s*$([regex]::Escape($label)):\s*(.+?)\s*$") { $Matches[1] } }
    $sdk = & $get 'SdkAddon Ver.'
    [pscustomobject]@{
        Path        = $Path
        ContentType = & $get 'Content Type'
        Sdk         = if ($sdk) { ($sdk -split ' ')[0] } else { $null }
        RightsId    = (& $get 'RightsId')
    }
}

# ---- the build's exefs (from 01-identify) -------------------------------------
$exefsSrc = Get-ChildItem $extracted -Recurse -Directory -Filter $Build -ErrorAction SilentlyContinue |
    Where-Object { $_.Parent.Name -eq 'exefs' } | Select-Object -First 1
if (-not $exefsSrc) { Fail "no extracted exefs for $Build (run 01-identify.ps1)" }
$sdkVersion = ($Build -split '_sdk')[1]
"Build:  $Build"
"Output: $Out"

# ---- program NCAs: the base game's and this build's ----------------------------
$programs = @(Get-ChildItem $extracted -Recurse -File -Filter '*.nca' | Where-Object { $_.DirectoryName -like '*\raw' } |
    ForEach-Object { Get-NcaHeader $_.FullName } | Where-Object { $_.ContentType -eq 'Program' })
$buildNca = $programs | Where-Object { $_.Sdk -eq $sdkVersion } | Select-Object -First 1
if (-not $buildNca) { Fail "no Program NCA with SDK $sdkVersion" }
# the base game: the program without title key crypto (cartridge) and the oldest SDK
$baseNca = $programs | Where-Object { -not $_.RightsId } |
    Sort-Object { try { [version]$_.Sdk } catch { [version]'0.0' } } | Select-Object -First 1
if (-not $baseNca) { Fail 'no base game Program NCA (the cartridge image)' }
"Base program:  $([IO.Path]::GetFileName($baseNca.Path)) (SDK $($baseNca.Sdk))"
"Build program: $([IO.Path]::GetFileName($buildNca.Path)) (SDK $($buildNca.Sdk)$(if ($buildNca.RightsId) { ", rights id $($buildNca.RightsId)" }))"

# ---- its title key: from any common ticket with the same rights id ---------------
$titleKey = $null
if ($buildNca.RightsId -and $buildNca -ne $baseNca) {
    foreach ($tik in Get-ChildItem $extracted -Recurse -File -Filter '*.tik') {
        $b = [IO.File]::ReadAllBytes($tik.FullName)
        if ($b.Length -lt 0x2C0 -or $b[0x281] -ne 0) { continue }  # (common tickets only)
        $rights = -join ($b[0x2A0..0x2AF] | ForEach-Object { $_.ToString('x2') })
        if ($rights -eq $buildNca.RightsId.ToLower()) { $titleKey = -join ($b[0x180..0x18F] | ForEach-Object { $_.ToString('x2') }); break }
    }
    if (-not $titleKey) { Fail "no ticket for rights id $($buildNca.RightsId)" }
}

# ---- exefs ---------------------------------------------------------------------
$exefsOut = Join-Path $Out 'exefs'
New-Item -ItemType Directory -Force $exefsOut | Out-Null
foreach ($f in Get-ChildItem $exefsSrc.FullName -File) {
    $dst = Join-Path $exefsOut $f.Name
    if ($Force -or -not (Test-Path $dst)) { Copy-Item $f.FullName $dst -Force }
}
"exefs:  $((Get-ChildItem $exefsOut -File | ForEach-Object Name) -join ', ')"

# ---- romfs\ (folder) --------------------------------------------------------------
$romfsDir = Join-Path $Out 'romfs'
if ($Force -or -not (Test-Path $romfsDir)) {
    "romfs:  extracting to $romfsDir (base + update patch; several minutes)..."
    New-Item -ItemType Directory -Force $romfsDir | Out-Null  # (hactool doesn't create it)
    $hargs = @('-k', $Keys, '--disablekeywarns', '-t', 'nca')
    if ($titleKey) { $hargs += "--titlekey=$titleKey" }
    if ($buildNca -ne $baseNca) { $hargs += "--basenca=$($baseNca.Path)" }
    $hargs += @("--romfsdir=$romfsDir", $buildNca.Path)
    $r = Invoke-Tool $Hactool $hargs
    if (-not (Get-ChildItem $romfsDir -ErrorAction SilentlyContinue)) {
        Write-Host ($r.Text -split "`n" | Select-Object -Last 15) -ForegroundColor Yellow
        Fail 'hactool did not extract the RomFS'
    }
}
$files = Get-ChildItem $romfsDir -Recurse -File
"romfs:  {0:N0} files, {1:N1} GB" -f $files.Count, (($files | Measure-Object Length -Sum).Sum / 1GB)

# ---- romfs.bin (image) --------------------------------------------------------------
$romfs = Join-Path $Out 'romfs.bin'
if ($Force -or -not (Test-Path $romfs)) {
    "image:  building $romfs..."
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & $RomfsImage $romfsDir $romfs 2>&1 | Select-Object -Last 2 | ForEach-Object { "        $_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    if ($code -ne 0) { Fail "romfs_image failed ($code)" }
}
# a RomFS image starts with its header size (0x50) as a u64
$head = New-Object byte[] 8
$fs = [IO.File]::OpenRead($romfs); [void]$fs.Read($head, 0, 8); $fs.Close()
$hdr = [BitConverter]::ToUInt64($head, 0)
"romfs:  {0:N1} GB, header size 0x{1:x} ({2})" -f ((Get-Item $romfs).Length / 1GB), $hdr, $(if ($hdr -eq 0x50) { 'ok' } else { 'NOT a RomFS image' })
if ($hdr -ne 0x50) { Fail 'romfs.bin is not a RomFS image' }
'Done. Next: make gen (port/), then make.'
