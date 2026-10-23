# Legal ground rules

These rules apply to every phase. They exist so the project stays on the same footing as published recompilation projects (N64Recomp, XenonRecomp): tooling ships, game content never does.

1. **Inputs come from your own hardware.** Dump the game from a cartridge or eShop purchase you own, on your own Switch (nxdumptool), and use your own console's keys (Lockpick_RCM). Copies obtained from anyone else are not a valid input.
2. **Nothing derived from the game is committed.** That covers keys, NSP/XCI/NCA files, NSO/ELF executables, RomFS assets, Ghidra databases of the game, and recompiler output generated from the game code. `.gitignore` blocks the common forms; review `git status` before every commit anyway.
3. **Publish tooling only.** If anything is released, it is the recompiler, runtime and scripts, which each user runs on their own dump.
4. **Respect upstream licenses.**
   - Ryujinx: MIT, usable as reference and, with attribution, as code.
   - yuzu and its forks: GPL. Don't copy GPL code into a non-GPL tree.
   - zeldaret/botw and open-ead libraries: check each repo's license before linking.
5. **Middleware stays opaque.** Havok and the Nintendo SDK are proprietary. They run as recompiled code from your dump, never as redistributed source or binaries.

This is not legal advice. Anti-circumvention law (for example DMCA §1201 in the US) varies by country, so check yours before dumping keys or publishing anything.
