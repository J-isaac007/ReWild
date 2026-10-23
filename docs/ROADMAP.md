# Roadmap: BotW (Switch) native PC port

Status as of 2026-10-23:
- P0–P1 done ([research/P1-identify.md](../research/P1-identify.md)).
- P2 baseline done for v1.5.0 ([research/P2-baseline.md](../research/P2-baseline.md)); its follow-ups and the v1.9.0 symbol port remain.
- **P3 recompiler question answered** ([research/P3-recompiler-eval.md](../research/P3-recompiler-eval.md)): nxrt's generator lifts all of v1.5.0 `main` with 0 unsupported instructions, and the C compiles. ReWild builds on nxrt/LASRecomp instead of a new recompiler.
- **Base chosen: LASRecomp's runtime.** The concrete plan for phases P3–P7 is [PORT-PLAN.md](PORT-PLAN.md).

## Target

| | |
|---|---|
| Game | *The Legend of Zelda: Breath of the Wild*, Switch, title ID `01007EF00011E000` |
| Preferred version | **v1.5.0**: the version [zeldaret/botw](https://github.com/zeldaret/botw) decompiles, so its symbols map 1:1 |
| Other versions | Any later update, up to the latest **v1.9.0** (2026-02-17; adds Thai text on Switch 2). Port decomp symbols from 1.5.0 first with Ghidra Version Tracking or BinDiff; the gap from 1.5.0 grows with each update |
| CPU | AArch64, main image base `0x7100000000`, 24.0 MiB of code, 113,491 functions |
| Host | Windows and Linux on x86-64; Vulkan first, DX12 optional |

`scripts/diag/01-identify.ps1` reads the actual version from the dump. Run it before choosing between the direct (v1.5.0) and symbol-porting paths. In v1.9.0, `control.nacp` compresses only its title block (u16 length + raw deflate); the version field stays plain. The script handles both layouts.

### Decompilation coverage

Bytes of code by status, from `data/uking_functions.csv` (re-measure with `research/decomp-status.ps1`):

| Status | Meaning | Share |
|---|---|---|
| O | matching source | 15.6% |
| m | minor mismatch | 0.5% |
| M | major mismatch | 0.6% |
| W | work in progress | 6.8% |
| L | library code (sead, nn SDK, Havok `hk*`, …) | 18.1% |
| U | not decompiled | 58.6% |

Roughly three quarters of the game has no source yet. A port that waits for the decomp is blocked for years, so the plan recompiles everything now and uses the decomp where it already exists.

## Strategy: hybrid static recompilation

1. **Recompile all AArch64 code to C++ offline.** Every function becomes a host function operating on a guest register context.
2. **Reimplement the platform under it:** Horizon kernel calls, Nintendo SDK services, the NVN graphics API, audio and input.
3. **Swap in decomp source function by function** as it reaches matching status, keeping the recompiled version as a regression oracle.

## Architecture

| Layer | Approach | Reference |
|---|---|---|
| Image loader (offline) | Parse the NSO set (`rtld`, `main`, `subsdk*`, `sdk`); decompress LZ4 segments; resolve dynamic imports and relocations at build time | nstool, Ghidra Switch Loader, nx-decomp-tools |
| CPU | **AArch64 → C static recompiler: nxrt's `tools/gen.py`** (Capstone + Unicorn; verified on BotW v1.5.0 `main`). The original plan was to write one. Guest context: X0–X30, SP, V0–V31 (128-bit), NZCV, FPCR/FPSR, TPIDRRO_EL0. Indirect branches (vtables, function pointers) go through an entry-point lookup table. Jump tables are recovered from Ghidra analysis. LDXR/STXR map to host atomics; NEON maps to SIMDe. FPCR flush-to-zero and default-NaN modes are honored | N64Recomp, XenonRecomp (design); Dynarmic or Unicorn (differential testing) |
| Memory | **Identity-mapped guest address space** (guest pointers are host pointers, so recompiled and decomp-native code can call each other directly). nxrt fixes the layout: main at `0x7000000000`, modules below `0x7080000000`. BotW therefore loads at `0x7000000000`, and decomp addresses map by subtracting `0x100000000` | nxrt `runtime/rt.h` |
| Decomp swap-in | Functions with status O/m are compiled from decomp source and replace their recompiled twins by address. Needs the LP64 data model: clang on Linux; on Windows `long` is 32-bit, so use clang with care or an explicit typedef layer | zeldaret/botw, open-ead/sead, open-ead/agl, open-ead/nnheaders |
| OS (HLE) | Kernel SVCs: memory, threads, synchronization, events, TLS. Services: implement at the **nn:: SDK function level** (replace functions in the `sdk` module) wherever possible, instead of emulating IPC | Ryujinx HLE (MIT) |
| Filesystem | Merge base + update + DLC RomFS read-only from the user's dump; mod overlay later. The game's own code still parses SARC/Yaz0/BYML/AAMP | — |
| Graphics | Intercept at the **NVN API** (the function table returned by the bootstrap loader), not the Maxwell GPU command stream. Implement NVN on Vulkan. Shaders are Maxwell SASS inside BNSH (within `.bfsha`), translated **offline** to SPIR-V with a pipeline cache. Textures: BNTX block-linear deswizzle; ASTC transcoded to BC7 | Ryujinx.Graphics.Shader (MIT); open-ead/agl for a later high-level rewrite |
| Audio | HLE `audren` with a software mixer, output through SDL3 | Ryujinx AudioRenderer (MIT); Nitr4m12/xlink2 |
| Input | HLE `hid` mapped to SDL3 gamepads, including gyro | — |
| Physics | Havok 2014 (`hk*`, statically linked) runs as ordinary recompiled code. Its determinism depends on bit-exact floating point | — |

Asset formats are read by the game's own code, so only GPU formats (BNTX textures, BNSH shaders) need converting. SARC/AAMP/BYML tools are for research and modding.

### Why Switch, not Wii U

| | Wii U | Switch |
|---|---|---|
| CPU | PowerPC 750 (Espresso), 32-bit big-endian | AArch64, 64-bit little-endian, same as x86-64 |
| GPU API | GX2 (Latte) | NVN (Maxwell) |
| Executable | RPX | NSO |
| Decompilation | none | zeldaret/botw |
| Existing runtime | Cemu (mature) | Ryujinx forks |

Little-endian 64-bit code needs no byte-swapping and has PC-compatible pointer sizes, and the decomp provides names and source. To simply play the game on PC, Cemu already exists; this project's goal is a native port.

## Phases

Each phase has an exit criterion. Don't start the next phase until it's met.

| Phase | Work | Exit criterion |
|---|---|---|
| **P0** Workspace & legal | Repo, `.gitignore`, docs | Scaffold committed; game data cannot enter git |
| **P1** Identify | Extract the dump; record title ID, game version, firmware version, NSO list, hashes, build IDs | `research/identity.json` exists; version known |
| **P2** Static baseline | Load `main` in Ghidra (Switch Loader); import decomp names from `uking_functions.csv` (port them to your version if it isn't 1.5.0); count functions, indirect-call sites, SVC sites, NVN entry points, `sdk` imports via GhidraMCP | `research/baseline.md` with those counts |
| **P3** Recompiler PoC | Lifter for a closed subset (leaf sead math and string functions); differential harness against Dynarmic/Unicorn with random inputs | ≥1,000 leaf functions bit-exact |
| **P4** Whole-binary build | All 113k functions emit and compile; runtime reaches `nnMain`; unimplemented SVCs and services are logged | Boots to the first service calls |
| **P5** OS HLE | Threads, sync, fs (RomFS), time, settings, applet and account stubs | First NVN command buffer submitted |
| **P6** Graphics | NVN on Vulkan; offline shader translation; textures | Nintendo logo, then title screen |
| **P7** Input & audio | hid and audren on SDL3 | Great Plateau playable for 10 minutes |
| **P8** Decomp swap-in | Link O/m functions from source; per-function regression harness (recompiled vs native) | Matching functions run as source |
| **P9** PC features | Resolution scaling, framerate decoupling (game logic assumes 30 fps), mod overlay, DX12 backend | Opt-in enhancements |

## Top risks

1. ~~No AArch64 static-recompilation framework exists.~~ Resolved: nxrt/LASRecomp exist (October 2026), and nxrt's generator handles BotW. The new top risk is **runtime coverage**:
   - BotW's SDK 4.4 services, its 93 direct `svc` sites and its NVN usage, on a runtime built from other games.
   - nxrt's Windows build has 8 compile errors; LASRecomp's Windows build works.
2. **NVN is undocumented.** nxrt/LASRecomp already implement NVN → Vulkan/Metal for several games. BotW-specific gaps must be found by running it.
3. **Shader translation coverage.** Maxwell SASS has many instructions; untranslated shaders block rendering. Mitigation: translate offline and report per-shader failures.
4. **Floating-point determinism.** Havok and game logic expect AArch64 FP behavior bit for bit.
5. **Version mismatch** with the decomp's v1.5.0.
