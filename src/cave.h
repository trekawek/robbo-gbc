#ifndef CAVE_H
#define CAVE_H
#include "level.h"

/* The live room: a copy of the level grid we mutate as the game plays.
   Indexed cave[y*LVL_W + x].  Bytes are ATASCII object codes. */
extern unsigned char cave[LVL_W * LVL_H];

#define CAVE_AT(x,y) cave[(unsigned int)(y)*LVL_W + (x)]

void cave_load(const Level *lvl);

/* read byte at x,y; returns OB_WALL for out-of-bounds (acts as solid border) */
unsigned char cave_get(signed char x, signed char y);
void cave_set(unsigned char x, unsigned char y, unsigned char b);

#endif
