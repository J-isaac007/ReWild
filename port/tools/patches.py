"""Per-instruction replacements applied by gen.py (guest address -> function(gen, addr) returning C code).

BotW v1.5.0 has none yet. LASRecomp's Link's Awakening patches (widescreen aspect sites, culling) were removed: they
assert LA instructions at fixed addresses and abort generation on any other game. Each BotW patch should check the
original instruction word first, so another build fails loudly instead of miscompiling.
"""

PATCHES = {}
