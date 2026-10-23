# Phase 3: existing recompiler evaluation (nxrt / LASRecomp)

Run on 2026-10-23. The question: can an existing Switch static recompiler handle BotW instead of writing one?

## Candidates compared

| | LASRecomp | nxrt | ssbu-recomp | kaiyao7/SwitchRecomp | bgyss/SwitchRecomp |
|---|---|---|---|---|---|
| Form | One game, finished app | Runtime library + game template | One game, finished app | Research framework | Specs + Rust skeleton |
| Runs a commercial game | Yes (beta) | Its code comes from 3 that do | Yes | No | No |
| Windows | Verified | Not verified | Primary | Tests only | No |
| License | MIT | Public domain + MIT parts | GPL-3.0 | GPL-3.0 | None |

Picked **nxrt** (LASRecomp + SkyRecomp + EoWRecomp merged into a library). Fallback: **LASRecomp**, same code lineage.

## Results

| Test | Result |
|---|---|
| nxrt `gen.py` on BotW v1.5.0 `main` | **36 s**; 114,056 functions (decomp lists 113,491), 533 C chunks, 449 MB of C; **0 unsupported instructions** (the 18 `rt_udf` are end-of-range traps); 6,693 indirect-branch dispatch sites, 1,169 jump tables recovered |
| Compile sample (6 of 534 chunks, nxrt `GENFLAGS`: `-O1 -march=x86-64-v3`, clang 23.1.3) | **All compile**; about 4.7 s per MB, i.e. ~35 core-minutes for the whole `main`, ~4–6 min on 12 threads |
| nxrt app build on Windows (MSYS2 UCRT64) | **Broken: 8 compile errors in 3 files**. `nxpkg.c`: `pread`, 2-argument `mkdir`. `menu.mm`: `strlcpy` (POSIX-only). `nvn/vulkan/exec.c`: `g_mirror_w` undeclared, a merge leftover, so the Vulkan backend hasn't compiled since the merge (Linux too). Link stage not reached |
| LASRecomp app build on Windows | **Works**: `lasrecomp.exe` built. Only `lasgame.dll` fails, as expected without Link's Awakening game files |

## Layout constraint

nxrt (like LASRecomp) uses a fixed guest layout (`runtime/rt.h`): modules below `HLE_BASE` `0x7080000000`, main at `0x7000000000`, alias region at `0x7100000000`. BotW therefore loads at `0x7000000000`, not the decomp's `0x7100000000`. Decomp addresses map by subtracting `0x100000000`.

BotW v1.5.0 module spans fit the default slots:

| Module | Span | Base |
|---|---|---|
| main | 38.7 MB | `0x7000000000` |
| sdk | 10.5 MB | `0x7004000000` |
| subsdk0 | 6.9 MB | `0x7005000000` |

## Conclusion

The recompiler side is **not** the blocker anymore. nxrt's generator handles all of BotW's code, and the C compiles. The work moves to the runtime:
- **OS services and NVN:** BotW's SDK 4.4 is older than the games nxrt was built from, and it uses a larger set of services and NVN features.
- **The 93 direct `svc` sites** in `main`.

Two ways to proceed:
1. Fix nxrt's 8 Windows errors (small portability patches; the Vulkan one needs a look at the merge) and upstream them.
2. Use LASRecomp's verified Windows runtime and port BotW onto it the way letsgo-recomp did.

## Reproduce

```bash
# generator (Windows Python venv with capstone, lz4, unicorn)
python C:\tools\src\nxrt\tools\gen.py main <exefs>\main 0x7000000000 generated\botw150-nxrt\main --simd c
# app builds: MSYS2 UCRT64 shell
cd /c/tools/src/nxrt/examples/game-template && make -j12
cd /c/tools/src/LASRecomp && make -j12
```
