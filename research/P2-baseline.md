# Phase 2: static baseline (v1.5.0 `main`)

Project: `ghidra/reWild.gpr` (git-ignored), program `main.nso` from `extracted/nso/01007ef00011e000_sdk4.4.0/`. Built 2026-10-23 with `scripts/ghidra/import-main.ps1`; counts from `scripts/ghidra/BaselineReport.java`.

## Import

| Step | Result |
|---|---|
| Loader | Switch Loader 1.6.1 (built from source for Ghidra 12.1.4), "Nintendo Switch Binary" |
| Decomp labels (pre-analysis) | 113,491 addresses, **all inside mapped memory**; 84,420 carry names (the rest are unnamed in the decomp) |
| Auto-analysis | 1,192 s (~20 min), succeeded |
| Post-pass | 774 functions analysis missed, created from decomp address + size; every decomp function tagged with its status |

## Counts

| Metric | Value | Why it matters |
|---|---|---|
| Functions | 114,670 | 113,491 from the decomp + **1,179 found only by Ghidra** (untagged), worth checking for decomp gaps |
| Thunks | 3,122 | Import stubs into `sdk`/`subsdk*`; the HLE boundary |
| Named functions | 84,448 (30,222 still default `FUN_…`) | |
| In a C++ namespace | 59,454 | Demangler turned decomp names into classes/namespaces |
| Instructions | 6,290,765 | Recompiler input size |
| `blr` (indirect calls) | 77,175 | Virtual calls and function pointers: all need the runtime entry-point lookup |
| `br` (indirect branches) | 8,133 | Jump tables and tail calls: need table recovery |
| `ldxr`/`stxr`, `ldaxr`/`stlxr` | 4,021/4,010, 2,332/2,332 | Exclusive-monitor atomics → host atomics |
| `svc` | 93 | Direct kernel calls inside `main` itself (not just the SDK); list them before P5 |
| External symbols | 614 | Imports from `sdk`/`subsdk*` |
| NVN externals | 1 | Only the bootstrap loader is imported; everything else goes through `nvnDeviceGetProcAddress`. That supports intercepting at the NVN API |
| Bad instructions | 9 | Places Ghidra couldn't decode; inspect (data in code, or unsupported encodings) |

Decomp status tags in Ghidra (filter with Window → Function Tags): `decomp_O` 32,213 · `decomp_m` 175 · `decomp_M` 360 · `decomp_W` 7,586 · `decomp_L` 13,632 · `decomp_U` 59,525.

## Follow-ups for P2 → P3

1. List the 9 bad instructions and the 93 `svc` sites (address, number, containing function).
2. Review the 1,179 Ghidra-only functions: are they real code the decomp's table lacks, or analysis false positives?
3. Load `sdk.nso` and `subsdk0.nso` from the same build to resolve the 614 imports, then map the NVN function table.
4. Pick the P3 PoC set: leaf functions tagged `decomp_O` that have no `blr`/`br`/`svc`/exclusives.
5. Port names to v1.9.0 with Version Tracking (1.5.0 → 1.9.0).

## Opening it

Close the headless run (done), then in the Ghidra GUI: File → Open Project → `C:\dev\OpenSourceProjects\reWild\ghidra\reWild.gpr`, open `main.nso`. With GhidraMCP enabled in that CodeBrowser, Claude can query it through the `ghidra` MCP server.
