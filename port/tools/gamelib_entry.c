// Entry of the game code library (runtime/gamelib.h), compiled into it with the generated code: the function
// tables of each module (GAME_MODULES, runtime/game_info.h), found by name when the app loads the library. Everything
// else in it stays hidden.
#include "game_info.h"
#include "gamelib.h"

#define X(m, base) extern const uint64_t m##_fn_addrs[], m##_fn_count; extern const GuestFn m##_fn_ptrs[];
GAME_MODULES
#undef X

GAMELIB_EXPORT const GameLibTables *lasgame_tables(void) {
#define X(m, base) {#m, m##_fn_addrs, m##_fn_ptrs, &m##_fn_count},
    static const GameLibModule mods[] = {GAME_MODULES};
#undef X
    static const GameLibTables t = {GAMELIB_ABI, GAMELIB_ID, sizeof mods / sizeof *mods, mods};
    return &t;
}
