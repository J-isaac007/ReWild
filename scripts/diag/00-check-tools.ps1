<#
.SYNOPSIS
  Reports which tools from docs\TOOLS.md are installed. Installs nothing.
#>
param([string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)

$ghidra = if ($env:GHIDRA_INSTALL_DIR) { $env:GHIDRA_INSTALL_DIR } else { 'C:\tools\ghidra_12.1.4_PUBLIC' }

function Find-Command([string[]]$Names) {
    foreach ($n in $Names) { $c = Get-Command $n -ErrorAction SilentlyContinue; if ($c) { return $c.Source } }
    $null
}

$checks = @(
    @{ Phase = 'P1'; Tool = 'hactool'; Path = (Join-Path $Root 'tools\bin\hactool.exe') }
    @{ Phase = 'P1'; Tool = 'nstool (optional)'; Path = (Join-Path $Root 'tools\bin\nstool.exe') }
    @{ Phase = 'P1'; Tool = 'prod.keys'; Path = (Join-Path $Root 'keys\prod.keys') }
    @{ Phase = 'P2'; Tool = 'Ghidra 12.1.x'; Path = (Join-Path $ghidra 'ghidraRun.bat') }
    @{ Phase = 'P2'; Tool = 'GhidraMCP extension'; Path = (Join-Path $env:APPDATA 'ghidra\ghidra_12.1.4_PUBLIC\Extensions\GhidraMCP') }
    @{ Phase = 'P2'; Tool = 'Ghidra Switch Loader'; Path = (@(
                (Join-Path $env:APPDATA 'ghidra\ghidra_12.1.4_PUBLIC\Extensions\SwitchLoader'),
                (Join-Path $ghidra 'Ghidra\Extensions\SwitchLoader')) | Where-Object { Test-Path $_ } | Select-Object -First 1) }
    @{ Phase = 'P2'; Tool = 'Python 3.12 (for oead)'; Path = $(try { (& py -3.12 -c 'import sys;print(sys.executable)' 2>$null) } catch { $null }) }
    @{ Phase = 'P3'; Tool = 'clang'; Path = (Find-Command 'clang', 'clang-cl') }
    @{ Phase = 'P3'; Tool = 'cmake'; Path = (Find-Command 'cmake') }
    @{ Phase = 'P3'; Tool = 'ninja'; Path = (Find-Command 'ninja') }
    @{ Phase = 'P3'; Tool = 'git'; Path = (Find-Command 'git') }
    @{ Phase = 'P6'; Tool = 'Vulkan SDK'; Path = $(if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { $null }) }
)

$rows = foreach ($c in $checks) {
    $ok = [bool]($c.Path -and (Test-Path $c.Path))
    [pscustomobject]@{ Phase = $c.Phase; Tool = $c.Tool; Status = $(if ($ok) { 'ok' } else { 'MISSING' }); Path = $(if ($ok) { $c.Path } else { '' }) }
}
$rows | Format-Table -AutoSize
$missing = @($rows | Where-Object Status -eq 'MISSING')
"{0} of {1} present. Sources for missing tools: docs\TOOLS.md" -f ($rows.Count - $missing.Count), $rows.Count
