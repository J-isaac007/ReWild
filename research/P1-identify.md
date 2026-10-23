# Phase 1: dump identification

Run on 2026-10-23 with `scripts/diag/01-identify.ps1` and `02-nso-survey.ps1`. Full detail is in `research/identity.json` and `research/nso-survey.json` (git-ignored, regenerate locally).

## Packages

| Package | Program ID | Builds inside |
|---|---|---|
| Base `.xci` | `01007ef00011e000` | **v1.0.0** (SDK 0.12.14) **and an embedded v1.5.0 update** (SDK 4.4.0) |
| Update `.nsp` | patch `01007ef00011e800` | **v1.9.0** (SDK 21.4.1, nnSdk 20.5.22); patch version 1114112 = 17 × 65536 |
| DLC 1 `.nsp` | `01007ef00011f001` | PublicData (AOC) |
| DLC 2 `.nsp` | `01007ef00011f002` | PublicData (AOC), 122 KB |

Firmware: **22.5.0** (platform NX; NCAs use key generation 22).

## Code per build (decompressed `.text` sizes)

| Module | v1.0.0 | v1.5.0 | v1.9.0 |
|---|---|---|---|
| main (`U-King.nss`) | 23,280,432 | **25,175,588** | 22,682,724 |
| sdk (nnSdk) | 3,473,812 | 4,252,832 | 6,698,752 |
| rtld | 12,092 | 6,112 | 9,536 |
| subsdk0 | 971,244 (NVN) | 3,186,728 (multimedia) | 3,501,312 (movie) |
| subsdk1 | 65,496 (nnDisplay) | — | — |
| subsdk2 | 176,956 (libcurl) | — | — |
| subsdk3 | 2,986,888 (multimedia) | — | — |

Build IDs (`main`):
- v1.0.0: `2094f152836683a3632c6153fd84ab8`
- v1.5.0: `16a91992bba71201e98756f3bc8f5d2f`
- v1.9.0: `cd57b23fa4bbad65803d9788c01821ee`

## Findings

1. **The decomp's target build is in this dump.** In v1.5.0 `main`, the code segment is 0x1802924 bytes. The decomp's last function (`hkpSampledHeightField_registerDdaRayCastFunction` at `0x71018025f8`, 44 bytes) ends at offset 0x1802624, inside the segment's tail padding. All 113,491 decomp names should apply to this build without porting. P2 starts from `extracted/nso/01007ef00011e000_sdk4.4.0/main.nso`.
2. **v1.9.0 is a substantially different binary:**
   - Its `main` is 2.5 MB smaller, and `.rodata` shrank from 11.9 MB to 6.1 MB.
   - The module-path record is gone.
   - `main` still links NintendoWare 0.11.30 libraries, while the SDK moved to 20.5.22.

   Porting symbols to it is a real P2 sub-task. Version Tracking should run 1.5.0 → 1.9.0.
3. **NVN is visible in every build:**
   - `nvnBootstrapLoader`, `nvnDeviceGetProcAddress` and `nvnQueueSubmitCommands` strings are present.
   - From v1.5.0 on, NVN lives inside `sdk` (`NintendoSDK_NVN-4_4_0`), not a separate subsdk.

   This confirms the plan to intercept at the NVN API.
4. **v1.0.0 already exists as a 64-bit build** (NX64 path, 64-bit `main`), so the whole Switch line is AArch64.
5. **Tooling notes:**
   - hactool 1.4.0 can't parse the 2026 patch NCA ("Invalid BKTR layout"), so NCA work uses nstool 1.9.2. hactool still unpacks packages and decompresses NSOs.
   - hactool also rejects 34-digit `*_kek_source` entries in current `prod.keys`, so the scripts pass it a filtered copy.
   - v1.9.0's `control.nacp` compresses only its title block (u16 length + raw deflate).
