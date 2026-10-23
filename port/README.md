# reWild port: BotW v1.5.0 on LASRecomp's runtime

A native PC build of *The Legend of Zelda: Breath of the Wild* (Nintendo Switch, v1.5.0) by static recompilation: the
game's AArch64 code is translated to C on your machine and compiled; this runtime stands in for the Switch. It is a lean
fork of [LASRecomp](https://github.com/peiikai/LASRecomp) (MIT) at commit `466fee6`, the way
[letsgo-recomp](https://github.com/raulzrrs/letsgo-recomp) forked it. What changed and why: [UPSTREAM.md](UPSTREAM.md).
The plan: [../docs/PORT-PLAN.md](../docs/PORT-PLAN.md).

**Status: B1 done.** The game library builds from your dump and loads; BotW's code starts and runs until its first
missing piece (`nn::vi::SetLayerScalingMode`, [../research/B1-first-boot.md](../research/B1-first-boot.md)). Next:
B2, the boot loop.

**No game files, keys or game code are included**, here or in any build. You need your own dump of BotW. The game code
is generated from it on your machine. See [../docs/LEGAL.md](../docs/LEGAL.md).

## Build (Windows)

MSYS2 UCRT64 with clang, lld, make, SDL3, FFmpeg, Vulkan, shaderc (the package list is in `tools/setup_windows.ps1`;
reWild's [../docs/TOOLS.md](../docs/TOOLS.md) records what this machine has). From a UCRT64 shell:

```bash
cd port
make -j      # -> build/botwrecomp.exe
```

Game code, from your own dump (reWild's `scripts/diag/01-identify.ps1`, then):

```bash
make romfs-image                                          # the RomFS image tool
powershell -File ../scripts/diag/03-prepare-game-files.ps1  # extracted/exefs, romfs/, romfs.bin (v1.5.0)
python -m venv tools/venv && tools/venv/Scripts/python -m pip install capstone lz4 unicorn
make gen     # C for main, sdk, subsdk0 (~1 min)
make -j      # -> build/botwgame.dll (~30 min on 12 threads)
build/botwrecomp.exe --play   # (PATH with C:\msys64\ucrt64\bin for the DLLs)
```

This tree keeps LF line endings (`.gitattributes` at the repository root); the bash tools break on CRLF.

## Files

| | |
|---|---|
| `runtime/game_info.h` | Who the game is: name, title id, v1.5.0 build id, file names, data folder |
| `runtime/hle/game_hooks.c` | Neutral versions of the hooks LASRecomp's Link's Awakening layer provided |
| `tools/patches.py` | Per-instruction patches for the generator: none yet |
| `README-LASRecomp.md`, `LICENSE` | LASRecomp's README (third-party component licenses and sources) and its MIT license |

## License

MIT, as LASRecomp ([LICENSE](LICENSE), © 2026 Peiikai, kept for everything derived from it). Third-party components
keep their own licenses ([README-LASRecomp.md](README-LASRecomp.md#license)). Nothing here grants rights to the game.
