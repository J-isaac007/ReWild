// romfs_image <romfs folder> <romfs.bin>: the RomFS image the runtime reads (runtime/services/fs.c), built from a
// folder of game files with the start screen's own builder (runtime/romfs_build.c). For setups that skip the start
// screen: ReWild's scripts/diag/03-prepare-game-files.ps1 (an update's patched RomFS only comes out as a folder).
// `make romfs-image` -> build/romfs_image(.exe).
#include <inttypes.h>
#include <stdio.h>

#include "romfs_build.h"

static void progress(uint64_t done, uint64_t total) {
    fprintf(stderr, "\rromfs: %" PRIu64 " / %" PRIu64 " MB", done >> 20, total >> 20);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <romfs folder> <romfs.bin>\n", argv[0]);
        return 2;
    }
    char why[256] = "";
    bool ok = romfs_build(argv[1], argv[2], progress, why, sizeof why);
    fprintf(stderr, "\n");
    if (!ok) {
        fprintf(stderr, "romfs_image: %s\n", why);
        return 1;
    }
    return 0;
}
