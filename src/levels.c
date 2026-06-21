#include <gb/gb.h>
#include "levels.h"

/* Maps a global planet index to its pack, switches in that pack's ROM bank,
   and returns the (now-mapped) Level pointer.  Computing &pack_levels[i] is a
   link-time address — no dereference here — so it is safe before the switch;
   the SWITCH_ROM makes the data readable by the caller. */
const Level *level_ptr(unsigned char idx) {
    if (idx < C1_NLEVELS) {
        SWITCH_ROM(BANK(c1_levels));
        return &c1_levels[idx];
    }
    idx -= C1_NLEVELS;
    if (idx < C2_NLEVELS) {
        SWITCH_ROM(BANK(c2_levels));
        return &c2_levels[idx];
    }
    idx -= C2_NLEVELS;
    SWITCH_ROM(BANK(c3_levels));
    return &c3_levels[idx];
}
