// Neutral versions of the game hooks the shared runtime calls (reWild B0).
//
// In LASRecomp these lived in its Link's Awakening layer (hle/{chamber,digs,doorjump,escape,flags_ext,hookshot,
// keysanity,miniboss,pets,pot_glow,qol,rescue,shopsanity,souls,warp,widescreen,mirror}.c, rando.c, the save editor):
// the renderer, input, file system and launcher call into it. Those files are gone; this is the call surface they
// left, each entry doing what the runtime does with no game feature active. BotW features replace entries here one at
// a time. Keeping the names keeps the diff against upstream LASRecomp small (port/UPSTREAM.md).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../cpu.h"

// ---- scripted test input and automation (runtime/hle/input.c; LAS defined these in doorjump.c / hookshot.c)
int32_t g_test_lx, g_test_ly;                   // stick override while frame < g_test_stick_until
uint64_t g_test_stick_until;
uint64_t g_test_buttons, g_test_buttons_until;  // LARECOMP_PLACE action scripts
int32_t g_stick_lx, g_stick_ly;                 // what the player holds this frame
uint64_t g_stick_freeze_until;                  // present count until which the stick reads neutral
char g_window_shot_request[256];                // window screenshot path, taken at the next present
void input_note_rstick(int32_t ry) { (void)ry; }

// ---- start screen (runtime/ui/launcher.mm): no game memory may be read while it is up
int g_start_screen;

// ---- per-frame game ticks (runtime/hle/input.c, runtime/nvn/vulkan/device.c)
void chamber_game_tick(Cpu *c, uint64_t frame) { (void)c, (void)frame; }
void digs_game_tick(Cpu *c, uint64_t frame) { (void)c, (void)frame; }
void give_item_tick(Cpu *c, uint64_t frame) { (void)c, (void)frame; }
void pets_game_tick(Cpu *c) { (void)c; }
void warp_game_tick(Cpu *c) { (void)c; }
void qol_tick(void) {}
void warp_frame(uint64_t frame) { (void)frame; }

// ---- input filters: buttons pass through unchanged
uint64_t pets_menu_input(uint64_t b, int32_t st[4]) { (void)st; return b; }
bool mirror_input(void) { return false; }

// ---- renderer (runtime/nvn/vulkan/exec.c, device.c): no tints, no extra passes, 16:9 framing
bool mirror_draw(bool composite) { (void)composite; return false; }
bool pets_hide_item_cursor(void) { return false; }
bool pot_glow_active(void) { return false; }
bool pot_level_active(void) { return false; }
bool pot_instance_gold(const float *m) { (void)m; return false; }
bool pot_tint(const float *m, float tint[8], uint64_t mesh) { (void)m, (void)tint, (void)mesh; return false; }
void pot_gold_tint(float tint[8]) { (void)tint; }
void escape_bottle_tint(int kind, const float *ui, uint32_t ui_size, float tint[8]) {
    (void)kind, (void)ui, (void)ui_size, (void)tint;
}
// Widescreen is LA's (patched camera aspect + per-room framing); off, output_scale() boxes the image to 16:9
bool widescreen_allowed(void) { return false; }
void widescreen_set_factor(float f) { (void)f; }
float widescreen_effect_stretch(uint64_t fs_hash) { (void)fs_hash; return 1.0f; }
void widescreen_frame_end(uint64_t frame) { (void)frame; }
void widescreen_backdrop_seen(uint64_t hash) { (void)hash; }
void widescreen_scene_ubo(const float *u, uint64_t size) { (void)u, (void)size; }
void widescreen_present_crop(uint64_t frame, double shape, float crop[4], double *vx, double *vy, double *vw,
                             double *vh) {
    (void)frame, (void)shape, (void)vx, (void)vy, (void)vw, (void)vh;
    crop[0] = crop[1] = 0, crop[2] = crop[3] = 1;  // the whole image, where output_scale() placed it
}
const char *game_level_name(void) { return "-"; }  // (logs; BotW: the current map once known)

// ---- file system (runtime/services/fs.c, runtime/nvn/texpatch.c): no seed, no overrides, no save listeners
void rando_boot(void) {}
const char *rando_seed_dir(void) { return NULL; }  // NULL: no randomizer seed active
bool rando_romfs_override(const char *path, char *out, size_t cap) { (void)path, (void)out, (void)cap; return false; }
void romfs_file_read_hook(const char *path) { (void)path; }
void flags_ext_save_read(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
}
void flags_ext_save_written(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
}
void save_editor_file_read(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
}
void save_editor_frame(uint64_t frame) { (void)frame; }
