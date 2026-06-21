#ifndef LEVEL_H
#define LEVEL_H

#define LVL_W 16
#define LVL_H 31

/* One Robbo room. grid holds LVL_W*LVL_H ATASCII object bytes (row-major).
   pal holds 8 BGR555 words: [0..3] normal palette, [4..7] inverse (PF3) palette. */
typedef struct {
    const unsigned char *grid;
    unsigned char screws;      /* number of screws to collect */
    unsigned char robbo_x;
    unsigned char robbo_y;
    unsigned int  pal[8];
} Level;

#endif
