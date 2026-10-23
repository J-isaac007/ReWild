# B2: boot loop log

The loop: `scripts/dev/boot-test.ps1` runs `port/build/botwrecomp.exe --play` from `port/`, stops it, and summarises the log, naming crash addresses and printing the exit code. Name `main` addresses with the decomp (`scripts/dev/decomp-name.ps1`), and see agl / ksys code as C with `scripts/ghidra/decompile.ps1` (v1.5.0 Ghidra project). Runtime fixes rebuild only the exe (seconds); `botwgame.dll` stays. Screenshots: `-Env @{LARECOMP_SHOT_AT='300,1200'}` writes `port/build/frames/frame_N.png`.

## Fixes (2026-10-23)

| # | Stop | Cause | Fix |
|---|---|---|---|
| 1 | Null deref in `nn::vi::SetLayerScalingMode` (`sdk+1d47ec`) | The SDK's own version reads the layer object that HLE `CreateLayer` only allocates | Host `SetLayerScalingMode` returning success (`hle/vi.c`), as in letsgo-recomp |
| 2 | `FATAL: nvn: out of command memory (need 80)` in `agl::DisplayList::endDisplayList` | agl gives a display list 512 bytes and an out-of-memory callback that adds nothing. Real NVN commands are a few words, but a LASRecomp `CmdRec` is 80+ bytes | Superseded by #7: records no longer live in command memory |
| 3 | Game abort: assertion in `nn::audio::RequestUpdateAudioRenderer` (from `Sound::init` → `aal` → `nn::atk::SoundSystem`) | The reply included the render-info section, which only exists from revision 5. BotW's renderer is **REV4** | Render info only for REV5+ (`services/audio.c`); the revision is now logged |
| 4 | Null deref in `nn::nfp::Initialize` (amiibo) | No `nfp:user` service: the logging stub returns no `IUser` | `services/offline.c`: `nfp:user` with no NFC device present; `csrng` (letsgo-recomp) |
| 5 | Hang: main thread in `nn::os::WaitForExitThread`, joining a thread that never ran | `pthread_create` failed with `EAGAIN` from about the 30th thread: MinGW commits each 256 MB guest-thread stack, and BotW creates ~40 threads. **Upstream LASRecomp has the same bug** | `plat_thread_spawn` / `plat_thread_join`: Windows reserves the stack (`STACK_SIZE_PARAM_IS_A_RESERVATION`); failures are logged |
| 6 | Host crash in the Vulkan executor (`exec_list`) following a garbage list pointer | Display lists recorded at once into shared command memory (below) | The executors check every record header; a bad list is logged and skipped. Root cause fixed by #7 and #8 |
| 7 | Corrupt display lists | Several threads record agl display lists into **the same** command memory at once; LASRecomp kept the records there | Records in runtime-owned per-list storage (`nvn/cmdbuf.c`); command and control memory only accounted. Both executors (`vulkan/exec.c`, `renderer.m`) find a list through `nvn_list_first(handle)` |
| 8 | Same lists again: one list recorded by several command buffers at once | agl gives each display list a slice of one shared **control** ring too, at its current offset, and moves the offset on only at `endDisplayList`. Lists begun at once on different threads start at the same position (presumably serialized on the console's per-core scheduling) | A list's place is (control memory position, command buffer); the handle is the runtime's own, one per place |
| 9 | Display lists re-recorded right after being called | `CallCommands` resolved the handle at execution (LASRecomp's `CMD_CALL`), when the game may already have re-recorded it | `CallCommands` / `CopyCommands` copy the called list's records into the caller when recorded, as NVN copies its entries |
| 10 | Texture upload from address `0 + offset` | A 120 MB **virtual** memory pool (flags `0x2a1`) has no storage until `nvnMemoryPoolMapVirtual` maps it | A virtual pool mapped whole onto one physical pool takes its address; textures and buffers made before are fixed up. Partial / sparse mappings are logged |
| 11 | `nvnSamplerCompare` read freed memory (`agl::driver::NVNMgr::registerSampler`) | agl keeps samplers **by value**; our `NVNsampler` held a pointer to a host descriptor freed at `Finalize` | The descriptor lives inside the 96-byte `NVNsampler` (at 8), as in the builder |
| 12 | Host heap corruption (exit `0xc0000374`) and faults on garbage `NvnTexture`s (`agl::driver::NVNMgr::registerTexture_`) | agl also registers **copies** of textures whose originals it finalized | `nvnTextureFinalize` drops the renderer image but keeps the `NvnTexture` for copies (counted in the log). Texture lookups check a magic (`nvn_texture`), and one NVN render lock keeps finalizes and submits from different threads out of an execution |

Debug switches added:
- `LARECOMP_DUMP_THREADS=<s>`: each guest thread's state, wait, `pc`/`lr`, and what `WaitSynchronization` waits on.
- `LARECOMP_LOG_CMDBUF=1`: command and control memory, recordings, calls.
- `make -B build/botwrecomp.exe GUARD_HEAP=1`: the runtime's C allocations on guard pages, with freed pages never reused (`runtime/platform/guard_heap.{h,c}`). It turns heap corruption into a fault at the guilty instruction; it found #11 and #12. Rebuild with `-B` without it afterwards.

## Where it stands

**B2 exit reached.** At frame ~300 BotW shows **"Nintendo Presents"**, and at frames ~800–1400 it plays the opening "...Open your eyes..." sequence, with the light-ray effect and subtitles. It runs at about 40–60 fps with up to ~230 draws per frame.

Boot path: SDK, subsdk0 and `main` constructors; system services; RomFS; `sead`/`ksys` app init; the audio system (REV4, 14 effects); amiibo init; ~40 threads; the NVN device and window; then frames.

## Open (B3)

- **Stop at ~frame 1500, 68 s:** guest access to `0xffffffffffffffff` in `nvnBufferGetAddress`, called from `gsys::ModelUnitDrawArray::drawEachNode_<false,true,true>` (`main+c4a69c`) with an `NVNbuffer*` of `0x0001000000000069`. This is when the Shrine of Resurrection's models start to draw. The value looks like packed data read as a pointer, which suggests an earlier HLE difference in model or buffer setup.
- Shader translator gaps, which so far only lose effects: `FS translate failed: unknown instruction` (e.g. `c8380206aff71014`, `df60000120670006`) and one compute shader (`ef5c000000870102`).
- `prepo:u` (play reports) is still the logging stub.
