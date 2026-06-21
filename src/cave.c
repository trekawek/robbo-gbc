#include "cave.h"
#include "object_tables.h"

unsigned char cave[LVL_W * LVL_H];

void cave_load(const Level *lvl) {
    unsigned int i;
    const unsigned char *g = lvl->grid;
    for (i = 0; i < LVL_W * LVL_H; i++)
        cave[i] = g[i];
}

unsigned char cave_get(signed char x, signed char y) {
    if (x < 0 || x >= LVL_W || y < 0 || y >= LVL_H)
        return OB_WALL;
    return cave[(unsigned int)y * LVL_W + x];
}

void cave_set(unsigned char x, unsigned char y, unsigned char b) {
    if (x >= LVL_W || y >= LVL_H) return;
    cave[(unsigned int)y * LVL_W + x] = b;
}
