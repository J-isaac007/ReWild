# Port plan: BotW v1.5.0 on LASRecomp's runtime

Decided 2026-10-23 after [research/P3-recompiler-eval.md](../research/P3-recompiler-eval.md): ReWild builds on
[LASRecomp](https://github.com/peiikai/LASRecomp) (MIT, commit `466fee6`), the way
[letsgo-recomp](https://github.com/raulzrrs/letsgo-recomp) put Pokémon Let's Go on it. This replaces phases P3–P7 of
[ROADMAP.md](ROADMAP.md) with concrete steps; P8 (decomp swap-in) and P9 (PC features) stay as planned.

## How letsgo-recomp did it

Their own analysis (`docs/ANALISE-LASRecomp.md`, 2026-10-14) found LASRecomp is not tied to Link's Awakening. Its core
is a generic NSO → C recompiler plus a runtime that reimplements Horizon OS and NVN by function name. The Zelda parts
(address patches, randomizer, gameplay HLE, UI tabs) are separable layers. Their recipe:

1. **Diagnose** with the generator before compiling anything: modules, sizes, unsupported instructions, missing imports.
2. **Lean fork:** copy the core, strip the Zelda layer, make the loader take more modules.
3. **Boot loop:** run, read `[hle] UNIMPLEMENTED` and crashes, implement, repeat. Goal: logo and title screen, even
   with broken graphics.
4. **Playable:** first area, menus, saves, audio. Then enhancements.

Pace: analysis on 14 Oct, working 3D gameplay by 17 Oct. Runtime fixes they needed (all MIT, reusable here):

| Area | Fix |
|---|---|
| Loader | 4 modules (`main`, `sdk`, `subsdk0`, `subsdk1`) instead of LAS's fixed `main` + `sdk` |
| Kernel | exception context (`svcGetInfo` 20); condvar timeout no longer reacquires the mutex |
| Services | Bluetooth offline (`btm:u`, `bt`), `csrng`, `hwopus` |
| Movies | `nn::movie` output buffers, stream end, core mask |
| NVN | per-stage programs, vertex-only programs, pacing by present interval |
| Shader translator | `LDC.64` whose address register is also the destination (skinned models) |
| Textures | `RGB5A1` / `RGBA4` → RGBA8; CPU-filled textures not scaled |
| Audio | minimum `audout` queue |

## Step 1 for BotW: diagnosis (done)

| Check | Result |
|---|---|
| Generator coverage | LAS's `gen.py` (with its Link's Awakening `patches.py` emptied) lifts all 114,056 functions of v1.5.0 `main` in 34 s with **0 unsupported instructions**; nxrt's generator gives the same result. The LAS patches assert LA instructions, so they must go: that's the first fork change |
| Generated C | Compiles with the runtime's flags (sample of 6 chunks); whole `main` ≈ 4–6 min on 12 threads |
| Modules (v1.5.0) | `main` 38.7 MB, `sdk` 10.5 MB, `subsdk0` (multimedia, `nn::movie`) 6.9 MB, `rtld`. LAS loads only `main` + `sdk` |
| Address layout | LAS: `MAIN_BASE` `0x7000000000`, `SDK_BASE` `0x7004000000` (64 MB slot), `HLE_BASE` `0x7080000000`. BotW fits; `subsdk0` goes at `0x7005000000`. Decomp addresses map by `- 0x100000000` |
| App build | LAS's Windows app builds on this machine (MSYS2 UCRT64, clang 23) |

### Services: BotW's allow-list vs LAS

From BotW v1.5.0's `main.npdm` (what the game may open; not all are used at boot):

| Status | Services |
|---|---|
| LAS has it | `aoc:u`, `apm`, `appletOE`, `audout:u`, `audren:u`, `fsp-srv`, `hid`, `lm`, `pctl`, `set`, `time:u`; `vi`, `pl` via HLE (`hle/vi.c`, `hle/pl.c`) |
| Verify | `acc:u0` (LAS has `services/acc.c`; check the name it registers) |
| Port from letsgo-recomp | `csrng`, `hwopus` |
| Offline stubs (no network) | `bcat:u`, `bsd:u`, `bsdcfg`, `friend:u`, `ldn:u`, `nifm:u` (report "no connection"), `nsd:u`, `ntc`, `prepo:u`, `sfdnsres`, `ssl` |
| Stubs ("not present") | `nfp:user`, `nfc:user`, `nfc:mf:u` (amiibo: "no tag"), `irs`, `mii:u`, `caps:su`, `audin:u`, `mm:u`, `pcm`, `banana` |
| Probably unused | `nvdrv` (LAS handles NVN at the API, above the driver), `ldr:ro` (NRO loading), `htc`, `htc:tenv`, `htcs` (dev host) |

### Version gap to watch

- **Older SDK:** LAS (v1.0.0, 2019) and Let's Go (2018) were built with newer SDKs than BotW 1.5.0's **SDK 4.4.0** (2018). Service command IDs and struct layouts of older SDK versions can differ. Expect fixes in `hid`, `applet` and `audren` revisions.
- **Direct kernel calls:** BotW's `main` makes **93 `svc` calls** directly ([P2-baseline.md](../research/P2-baseline.md)). LAS's kernel implements ~40 SVCs; check the list against them first.

## Repository layout

Like letsgo-recomp: a copy of LASRecomp's core at a pinned commit, with its MIT notice, inside ReWild.

```
ReWild/
  port/                 lean fork of LASRecomp @ 466fee6 (LICENSE-LASRecomp kept)
    Makefile            GAME = botw; modules main/sdk/subsdk0
    runtime/            LAS runtime minus the Zelda layer, plus BotW hooks
    tools/              gen.py etc.; patches.py starts empty
  scripts/diag/         existing diagnostics
  docs/, research/      unchanged
```

`port/UPSTREAM.md` records the base commit and every intentional divergence, so LASRecomp and nxrt fixes can be merged later. (nxrt is LASRecomp generalized; if its Windows build gets fixed, `port/` can become an nxrt game folder.)

## Phases

### B0: lean fork (exit: `make` builds `botwrecomp.exe` with no Zelda code) — done 2026-10-23

`port/` builds `botwrecomp.exe` cleanly. The LA layer is gone; the 57 hooks the shared runtime still calls are neutral stubs in `runtime/hle/game_hooks.c`. Details and leftovers: [port/UPSTREAM.md](../port/UPSTREAM.md). Original checklist:
- Copy `runtime/`, `tools/`, `Makefile`, `fonts/` and the needed `third_party/` from LASRecomp `466fee6` into `port/`.
- Remove the Zelda layer:
  - `runtime/hle/{chamber,digs,doorjump,escape,flags_ext,hookshot,keysanity,miniboss,pets,pot_glow,qol,rescue,shopsanity,souls,warp,widescreen}.c`, `runtime/rando.*`, `tools/rando/`, `third_party/LAS-Randomizer`.
  - The LA patches (`tools/patches.py` → empty) and the LA culling and texture patches in `nvn/`.
  - The randomizer, warp, tracker and save-editor UI tabs.
- Identity:
  - `BUILD_ID` = first 8 bytes of v1.5.0 `main`'s build id (`16a91992bba71201`), "needs v1.5.0" message.
  - Title id `01007EF00011E000`, app name, data folder, ini name.
- Makefile: game id `botw`; `GAMELIB_SRCS` without `rando/patch_variants.json`.

### B1: loader and game code (exit: `botwgame.dll` builds from your dump and loads) — done 2026-10-23

`botwgame.dll` (213 MB) builds from the v1.5.0 dump and loads. All three modules map and run their constructors. BotW's code then runs until a null `nn::vi::Layer` in `SetLayerScalingMode`, B2's first item. Details: [research/B1-first-boot.md](../research/B1-first-boot.md). Original checklist:
- Loader: a module list (`main` `0x7000000000`, `sdk` `0x7004000000`, `subsdk0` `0x7005000000`). Port letsgo-recomp's 4-module change, or nxrt's game-library format v2 (`gamelib.*`, `dispatch.c`).
- `make gen` for all three modules; build `botwgame.dll` (expect ~5–8 min with 12 threads).
- Game files: a ReWild script that writes `exefs/` (the v1.5.0 one) and the **v1.5.0 RomFS**. The 1.5.0 update inside your cartridge image patches the base RomFS (BKTR), so it needs `hactool --basenca`. Output is about 14 GB.

### B2: boot to the first frame (exit: the Nintendo logo appears) — done 2026-10-23

BotW shows **"Nintendo Presents"** and plays the opening "...Open your eyes..." sequence, at about 40–60 fps. Twelve stops fixed:
- `vi`, audio REV4, `nfp`, Windows thread stacks.
- NVN command storage redesigned. agl records display lists on several threads into shared command *and* control memory, so lists now live in runtime-owned storage per place, and calls copy the called records when recorded.
- Virtual memory pools.
- Samplers and textures copied by value.
- A render lock.

It stops at ~frame 1500, in the model renderer (`gsys::ModelUnitDrawArray::drawEachNode_`, a garbage `NVNbuffer*`), as the Shrine of Resurrection loads; that's B3's first item. Log: [research/B2-boot-log.md](../research/B2-boot-log.md). Original checklist:
- Add the stubs and ports from the services table above.
- Compare the 93 `svc` sites with `runtime/kernel.c`; add the missing ones.
- Loop: run → `[hle] UNIMPLEMENTED` / crash → implement → repeat, the letsgo-recomp way. Keep a running list in `research/B2-boot-log.md`.

### B3: title screen and Great Plateau (exit: walk around the Great Plateau)
- NVN and shader-translator gaps. BotW uses far more shaders than LA or Let's Go, so cache the translations.
- Audio: LAS's `audren` mixer has **no effects or splitters**, and BotW likely uses reverb. Implement them or stub them at first.
- `nn::movie` from `subsdk0` (opening and memory cutscenes).
- Saves: the `save/` layer and BotW's save format, unchanged.

### B4: playable (exit: first shrine, divine-beast intro, a save/load cycle)
- Havok physics stability (FP determinism), streaming of the large world, memory use (BotW expects about 3.2 GB of the Switch's 4 GB).
- DLC: `aoc:u` with the DLC RomFS (`01007ef00011f001`/`f002`).
- Then the roadmap's P8 (decomp swap-in) and P9 (PC features); v1.9.0 support after symbols are ported.

## Legal

The same model as LASRecomp and letsgo-recomp ([LEGAL.md](LEGAL.md)): no game files, keys or generated code in git or in releases. The game code is built on the player's machine from their own dump. Keep LASRecomp's MIT notice (`LICENSE-LASRecomp`) on everything derived from it.

## Next actions

1. ~~B0: create `port/` from LASRecomp `466fee6`, strip the Zelda layer, rename, build.~~ Done.
2. ~~Write the game-files script~~ Done: `scripts/diag/03-prepare-game-files.ps1`.
3. ~~B1 loader change, first `make gen` with all three modules~~ Done.
4. ~~B2: `SetLayerScalingMode`, then the boot loop to the Nintendo logo~~ Done.
5. B3: the garbage `NVNbuffer*` in `gsys::ModelUnitDrawArray::drawEachNode_` (~frame 1500, Shrine of Resurrection), then the shader-translator gaps (`FS translate failed: unknown instruction`).
