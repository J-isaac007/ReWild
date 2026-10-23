# Upstream

Base: [LASRecomp](https://github.com/peiikai/LASRecomp) commit `466fee6` ("Merge pull request #321"), copied with
`git -c core.autocrlf=false archive` on 2026-10-23. Every intentional difference is listed here, so upstream (and nxrt,
which merged LASRecomp, SkyRecomp and EoWRecomp) fixes can be brought in later: diff a file against `466fee6`, take what
isn't one of these.

## Removed: the Link's Awakening layer

| What | Files |
|---|---|
| Gameplay features (randomizer logic, QoL, pets, souls, pots, warp, digs, door shuffle, mirror mode, widescreen) | `runtime/hle/{chamber,digs,doorjump,escape,flags_ext,hookshot,keysanity,miniboss,pets,pot_glow,qol,rescue,shopsanity,souls,warp,widescreen,mirror}.c`, `runtime/rando.c` |
| Randomizer | `third_party/LAS-Randomizer`, `tools/rando/` |
| UI pages and overlays | `runtime/ui/{chaos_overlay,cheats,difficulty,inventory_overlay,pets_ui,rando_tab,save_editor,shader_tour,spoiler_view,tracker_overlay,warp_tab}.mm`, `item_names.h`, `warp_data.h` |
| LA test tools | `tools/{doorjump_test.sh,explore_far.sh,explore_input.sh,house_input.txt,warp_table.py}` |
| LA screenshots, CI, contributing guide | `docs/{start_screen,ultrawide}.jpg`, `.github/`, `CONTRIBUTING.md` |
| LA generator patches (widescreen aspect sites): they assert LA instructions and abort generation on BotW | `tools/patches.py` (now empty) |

## Added

| File | Why |
|---|---|
| `runtime/hle/game_hooks.c` | The 57 functions and globals the shared runtime (renderer, Vulkan backend, input, fs, launcher) still calls, found by linking without the LA layer. Each does what the runtime does with no feature active. BotW features replace entries one at a time. Names kept, so upstream files merge without edits |
| `runtime/game_info.h` | Game identity in one place, and the module list (`GAME_MODULES`) |
| `tools/romfs_image.c` | `make romfs-image`: the start screen's RomFS image builder as a command-line tool. hactool writes an update's patched RomFS only as a folder |
| `README.md`, `UPSTREAM.md` | This port's docs; LASRecomp's README is `README-LASRecomp.md` |

## Changed

| File | Change |
|---|---|
| `runtime/kernel.c` | `svcGetInfo` 18 (program id): `GAME_TITLE_ID` instead of LA's `0x01006BB00C6F0000` |
| `runtime/ui/launcher.mm` | Build id check against BotW v1.5.0; version text; title; update feed → reWild's releases; version tag `alpha_b0` (never outranked by an update) |
| `runtime/ui/menu.mm` | Tabs: Controls, Video, Audio, Quit (ids unchanged). Removed: Save Editor, Randomizer, Warp, Cheats, Gameplay, Cosmetics; the LA overlays; Video's Aspect ratio / Remove blur / Hide Hero Mode icon (LA patches) |
| `runtime/gamelib.h`, `gamebuild.c`, `paths.{c,h}`, `settings.c`, `platform/{windows,linux}.c`, `nvn/vulkan/device.c` | Names from `game_info.h`: `botwrecomp(.exe/.log/.ini)`, `botwgame.dll`, data folder, bundle id, message boxes, window title |
| `Makefile` | Output names; `all` builds the game library only when there is generated code (`$(strip)`: upstream builds it with none and fails); no randomizer helper (gen helper venv: `build/gen-venv`); `--error-limit=0` |
| `tools/lasgen.spec` | No `rando/patch_variants.json` in the bundle |
| **B1: modules as a list** (LASRecomp: fixed `main` + `sdk`; BotW v1.5.0 also runs `subsdk0`) | Same design as nxrt's game library format 2 |
| `runtime/gamelib.h`, `tools/gamelib_entry.c`, `gamelib.c` | `GAMELIB_ABI` 2: a list of per-module function tables, built from `GAME_MODULES` |
| `runtime/loader.c` | Loads every module in `GAME_MODULES` (`main` and `sdk` required, a missing subsdk is left out); fails if a module overruns its slot |
| `runtime/dispatch.c` | One lookup table per module |
| `runtime/main.c` | Module init: every module but `main` at the SDK's point, then `main` (LASRecomp: `sdk`, then `main`) |
| `runtime/gamebuild.c` | The start screen's setup generates and compiles every module; `--hle` for `sdk` only |
| `runtime/rt.h` | `g_modules[GAME_MAX_MODULES]` |
| `Makefile` | `GAME_MODULES` (same list as the header); `make gen` and the game library cover every module; `game_info.h` in `GAMELIB_SRCS` and the release's `gamelib-src/`; `make romfs-image` |

### B2 (boot loop: `research/B2-boot-log.md`)

| File | Change |
|---|---|
| `runtime/hle/vi.c` | Host `nn::vi::SetLayerScalingMode` (from letsgo-recomp) |
| `runtime/nvn/cmdbuf.c`, `nvn.h` | **Command storage redesign.** Records live in runtime-owned per-list storage (chunks chained with `CMD_JUMP`), one list per place: (control memory position, else command memory position; command buffer). The handle is the runtime's own (`0x7F00000000`+). Command and control memory are only accounted, never written. `nvn_list_first(handle)` is how executors find a list. `CallCommands` / `CopyCommands` copy the called records in when recorded (upstream: `CMD_CALL` resolved at execution). `LARECOMP_LOG_CMDBUF` |
| `runtime/nvn/vulkan/exec.c`, `nvn/renderer.m` | `exec_list` resolves handles through `nvn_list_first` and checks record headers (a bad list is logged and skipped); Vulkan: render targets that are no longer textures are dropped |
| `runtime/nvn/objects.c`, `nvn.h` | Virtual memory pools (`nvnMemoryPoolMapVirtual`, whole contiguous mappings: textures / buffers made before are fixed up); `NVNsampler` holds its descriptor in place (games copy samplers by value); `nvnTextureFinalize` keeps the `NvnTexture` for copies (drops the renderer image), `NvnTexture.magic` + `nvn_texture()` lookups; `render_lock` around submit, present, finish and object finalizes (BotW finalizes on loading threads) |
| `runtime/platform/guard_heap.{h,c}` (new), `Makefile` | `GUARD_HEAP=1` debug build: the runtime's C allocations on guard pages |
| `runtime/services/audio.c` | Render info only for revision 5+ (BotW: REV4); revision in the renderer log line |
| `runtime/services/offline.c` (new) | `nfp:user` (no NFC device), `csrng` (from letsgo-recomp) |
| `runtime/platform.h`, `platform/{windows,linux,macos}.c` | `plat_thread_spawn` / `plat_thread_join`: Windows reserves guest-thread stacks instead of committing 256 MB each (upstream LASRecomp has the same `pthread_create` + `EAGAIN` problem with many threads) |
| `runtime/kernel.{c,h}` | Guest threads through `plat_thread_spawn`, failures logged; `LARECOMP_DUMP_THREADS` thread dump (wait state, sync handles) |
| `tools/sym.py` | Knows `subsdk0` |

Kept as is: the `LARECOMP_*` environment variables (debug and test switches), internal names (`lasgen`, `lasgame_tables`), `docs/BUILD-windows.md`, `docs/linux.md`.

## Known leftovers (later phases)

- **Start screen:** the ‹ Vanilla / seed › picker is still there. It lists nothing without a `rando/` folder, so it always shows Vanilla. Remove it when the launcher gets BotW's own layout.
- **Settings:** `Settings` still has LA-only fields (`tunic`, `hearts`, `hide_hero_icon`, `blur_removal`, `aspect`, `rando_seed`). They're unused by BotW; removing them changes the ini format, so it waits.
- **Texture patching:** `runtime/nvn/texpatch.c` and the LA-specific branches in the renderer (`renderer.m`, `vulkan/exec.c`: pot / bottle / mirror tints, LA shader hashes) stay behind the neutral hooks.
- **macOS / Linux packaging:** `tools/bundle_macos.sh`, `tools/linux/{appimage,package,rando_helper}.sh` and `tools/build_deps_macos.sh` still expect the randomizer helper (`rando-gen`). Only the Windows build is maintained for now.
- **App icon:** `docs/icon/` is LASRecomp's.
