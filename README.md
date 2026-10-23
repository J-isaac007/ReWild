# ReWild

Research toward a native PC (Windows/Linux) port of *The Legend of Zelda: Breath of the Wild* (Nintendo Switch build) via static recompilation, with incremental swap-in of the community decompilation.

**This repository contains no game code, assets or keys, and never will.** Everything here is tooling and documentation that operates on a dump you made from your own console and cartridge. See [docs/LEGAL.md](docs/LEGAL.md).

## Layout

| Path | Purpose |
|---|---|
| [docs/ROADMAP.md](docs/ROADMAP.md) | Architecture and phased plan |
| [docs/PORT-PLAN.md](docs/PORT-PLAN.md) | Concrete plan: BotW v1.5.0 on LASRecomp's runtime |
| [docs/TOOLS.md](docs/TOOLS.md) | Tool inventory and where each comes from |
| [docs/LEGAL.md](docs/LEGAL.md) | Ground rules for inputs and publishing |
| `research/` | Measurements and findings (generated JSON is git-ignored) |
| `scripts/diag/` | Diagnostic scripts, run in order |
| `keys/`, `dumps/`, `extracted/`, `tools/bin/` | Local only, git-ignored |

## Putting your files in place

```
keys\prod.keys          your console's keys (Lockpick_RCM); title.keys too, if you have it
dumps\base\             base game .xci
dumps\update\           update .nsp
dumps\dlc\              DLC .nsp files
dumps\firmware\         firmware NCAs (reference only)
```

## Diagnostics

```powershell
.\scripts\diag\00-check-tools.ps1     # what is installed / missing
.\research\decomp-status.ps1          # zeldaret/botw decompilation progress
.\scripts\diag\01-identify.ps1        # title ID, game + firmware version, NSO hashes
.\scripts\diag\02-nso-survey.ps1      # NSO segments, build IDs, SDK version
```
