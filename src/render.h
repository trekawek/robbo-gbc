#ifndef RENDER_H
#define RENDER_H
#include "level.h"

/* 16x16 cells: each cave cell = a 2x2 block of 8x8 font tiles.
   Playfield fills the 32-wide BG map (16 cells); the viewport scrolls in both
   axes to follow Robbo. Vertically the cave is 62 tile-rows but the BG map is
   only 32, so rows are streamed into a 16-cave-row rolling window. */

#define HUD_PX 128            /* HUD window top (bottom 2 tile rows) */
#define PLAYFIELD_PX 128      /* visible playfield height (above HUD) */

void render_init(void);                            /* load tiles, once at boot */
void render_load_level(const Level *l, unsigned char idx); /* palettes, wall, window, camera */
void render_cell(unsigned char x, unsigned char y);        /* repaint one cave cell */
void render_repaint(void);                                 /* reload all visible rows */
void render_update(unsigned char robbo_x, unsigned char robbo_y); /* ease camera + stream rows */
void render_snap(unsigned char robbo_x, unsigned char robbo_y);   /* jump camera (teleport fx) */
void render_anim(unsigned char facing, unsigned char moving); /* robbo facing + tile animation */

#endif
