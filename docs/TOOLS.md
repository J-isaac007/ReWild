# Tool inventory

`scripts/diag/00-check-tools.ps1` reports which of these are present. Local binaries go in `tools/bin/` (git-ignored).

## Already installed (2026-10-23)

| Tool | Location | Use |
|---|---|---|
| Ghidra 12.1.4 | `C:\tools\ghidra_12.1.4_PUBLIC` | Static analysis |
| JDK 25 | `C:\Program Files\Java\jdk-25.0.4` | Ghidra runtime |
| GhidraMCP 2.0 | Ghidra user extension + `C:\tools\ghidra-mcp\venv` | Claude drives the open Ghidra window (`ghidra` MCP server) |
| REA 6.7.0 | `npx rea-agents` (`rea` MCP server) | Evidence-backed analysis. On Windows its Ghidra provider only admits PE and raw 6502 files, so it can't analyze Switch NSO/ELF here; use WSL/Linux for that |
| Python 3.14, Node 24, git | on PATH | Scripts |

## Phase 1–2: extraction and static analysis

| Tool | Source | Use | Note |
|---|---|---|---|
| hactool 1.4.0 | github.com/SciresM/hactool (release `hactool-1.4.0-win.zip`) | Unpack XCI/NSP; decompress NSOs | Replaces hactoolnet (the LibHac repository is no longer on GitHub). Can't parse 2026-era patch NCAs ("Invalid BKTR layout") and rejects 34-digit `*_kek_source` keys; the scripts work around both |
| nstool 1.9.2 | github.com/jakcron/nstool (release `win_x64`) | Read and extract NCAs (ExeFS, control), NSO API/symbol lists | Handles the v1.9.0 patch NCA |
| Ghidra Switch Loader | github.com/Adubbz/Ghidra-Switch-Loader | Load NSO/NRO/KIP directly in Ghidra | Latest release targets Ghidra 12.1.2, and extensions are version-locked. **Built from source** at `C:\tools\src\Ghidra-Switch-Loader` (commit 5d80d19) with Ghidra's bundled Gradle 9.7.1. The repo's own wrapper (8.10.2) can't run on JDK 25. One patch: `build.gradle` used `project.exec`, removed in Gradle 9, so it now uses `providers.exec`. Build: `set GHIDRA_INSTALL_DIR=…` then `<ghidra>\support\gradle\gradlew.bat -p C:\tools\src\Ghidra-Switch-Loader`. Installed in the user Extensions folder and in the headless settings folder |
| nx2elf | github.com/shuffle2/nx2elf | NSO → ELF, only for tools that can't read NSO | Not needed for Ghidra: hactool decompresses NSOs (`02-nso-survey.ps1`). No release; build from source |

### Ghidra headless quirk (12.1.4 + JDK 25 on Windows)

Headless runs that execute Java scripts fail in Felix (Ghidra's OSGi framework) with "The data file must be inside the data dir" when using the default settings folder (`%APPDATA%\ghidra`). Pointing `APPDATA` at a separate folder works around it, the same way REA runs Ghidra. `scripts/ghidra/import-main.ps1` uses `C:\tools\ghidra-headless-appdata`. The GUI is unaffected.

## Assets (research and modding)

| Tool | Source | Use |
|---|---|---|
| oead | github.com/zeldamods/oead | SARC, Yaz0, BYML, AAMP in Python. Wheels lag new Python versions, so use a Python 3.12 venv |
| Switch Toolbox | github.com/KillzXGaming/Switch-Toolbox | View BFRES models, BNTX textures |
| BCML / EventEditor | github.com/NiceneNerd/BCML, github.com/zeldamods/event-editor | Mod merging; EventFlow (`.bfevfl`) |

## Phase 3+: recompiler and runtime

Installed 2026-10-23:

| Tool | Location | Note |
|---|---|---|
| nxrt | `C:\tools\src\nxrt` (commit ffa2315, LF line endings) | Recompiler (`tools/gen.py`) + runtime. Its generator venv (capstone 5.0.7, lz4, unicorn 2.1.4) is in `examples/game-template/tools/venv` (Python 3.14) |
| LASRecomp | `C:\tools\src\LASRecomp` (commit 466fee6) | Fallback runtime; Windows app builds |
| MSYS2 UCRT64 | `C:\msys64` | clang/lld 23.1.3, make, SDL3 3.4.18, FFmpeg, Vulkan 1.4.363, shaderc 2026.4, spirv-tools, VMA, validation layers, nasm, gdb. Build from a UCRT64 shell. Driving bash from PowerShell needs a script file, since quoting gets mangled |

Originally planned (now mostly covered by nxrt):

| Tool | Use |
|---|---|
| LLVM/clang 18+, CMake, Ninja | Build the recompiler and generated code (LP64 needed for decomp swap-in) |
| Visual Studio Build Tools | Windows SDK, linker |
| Vulkan SDK, SDL3 | Graphics, windowing, input, audio output |
| Dynarmic or Unicorn | Reference AArch64 execution for differential tests |
| SIMDe | NEON on x86 |
| Ryujinx source (read-only reference) | HLE services, shader translator, audio renderer (MIT) |
| zeldaret/botw (+ submodules) | Symbols, decomp source, sead/agl/nnheaders |

## On your Switch (dumping)

| Tool | Source | Use |
|---|---|---|
| nxdumptool | github.com/DarkMatterCore/nxdumptool (archived) | Dump cartridge, update and DLC |
| Lockpick_RCM | community-maintained forks | Dump your console's keys |
