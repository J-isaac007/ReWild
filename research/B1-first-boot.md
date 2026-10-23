# B1: game code built and loaded (first boot)

2026-10-23. Port at `port/` (LASRecomp `466fee6` lean fork + module list).

## What was built

| Step | Result |
|---|---|
| Game files (`scripts/diag/03-prepare-game-files.ps1`) | `port/extracted/exefs/` = v1.5.0 (`main`, `sdk`, `subsdk0`, `rtld`, `main.npdm`). `romfs/` = base cartridge RomFS + v1.5.0 patch (BKTR, via hactool `--basenca`): 41,052 files, 14.1 GB, `System/Version.txt` = `1.5.0`. `romfs.bin` built from it by `make romfs-image` (header ok) |
| `make gen` (3 modules) | 69 s. `main` 534 files / 427 MB, `sdk` 123 / 75 MB, `subsdk0` 116 / 57 MB of C; **0 unsupported instructions** in all three |
| `make -j12` → `build/botwgame.dll` | 213 MB, ~33 min on 12 threads (while the RomFS extraction ran on the same disk); `GAMELIB_ID` `49741e93b558b745` |

## First boot (`botwrecomp.exe --play`, `LARECOMP_NO_LAUNCHER=1`)

```
[gamelib] build\botwgame.dll (functions: main 114056, sdk 28228, subsdk0 13329)
loaded main    at 7000000000 size 26ac000
loaded sdk     at 7004000000 size a8c000
loaded subsdk0 at 7005000000 size 6e8000
relocated main (35 imports bound to host stubs), sdk (0), subsdk0 (1)
init sdk (105 init_array entries), init subsdk0 (45), init main (1122)
services: lm, apm, appletOE, aoc:u, pctl (+ :s :a :r), fsp-srv
[lm] ... U-King/trunk/Game/Lib/sead/.../seadRawPrintNin.cpp | PrintStringImpl | MainThread
[fs] romfs index: 41052 files; first reads served
*** exception c0000005 accessing 0  in f_70041d47ec
```

**B1 exit criterion met:** the game library loads, and all three modules map, relocate and run their constructors. BotW's own code then runs: the `sead` engine logs, and RomFS reads are served.

## First B2 item

The crash: `nn::vi::SetLayerScalingMode(nn::vi::Layer*, nn::vi::ScalingMode)` (`sdk+1d47ec`, from `sdk+1d4818`) with a null layer (`x0 = 0`), on the main thread. letsgo-recomp hit the same thing and lists the fix as "`vi`: `SetLayerScalingMode`" in `runtime/hle/vi.c`. Start B2 there.

Other notes for B2:
- `main` has 35 imports bound to host stubs. List them with `LARECOMP_LOG_*` / the `[hle] UNIMPLEMENTED` lines as they're hit.
- `[am]` `ISelfController` commands 11–16 and `IWindowController` 10 are unhandled but answered ok. They're expected on SDK 4.4.
- `tools/sym.py` now knows `subsdk0` too: `cd port/tools && venv/Scripts/python sym.py <addr>`.
