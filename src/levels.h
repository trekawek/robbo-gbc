#ifndef LEVELS_H
#define LEVELS_H

#include "level.h"
#include "gen/levels_c1.h"
#include "gen/levels_c2.h"
#include "gen/levels_c3.h"

/* All planet packs concatenated into one continuous 0..NLEVELS-1 sequence. */
#define NLEVELS (C1_NLEVELS + C2_NLEVELS + C3_NLEVELS)

/* Activate the ROM bank holding planet `idx` and return a pointer to its Level.
   The pointer is only valid while that bank stays mapped (i.e. until the next
   level_ptr / SWITCH_ROM), which is fine: it is consumed immediately at level
   load to copy the grid into RAM and read the palette. */
const Level *level_ptr(unsigned char idx);

#endif
