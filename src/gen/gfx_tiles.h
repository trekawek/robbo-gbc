#ifndef GFX_TILES_H
#define GFX_TILES_H
#include <gb/gb.h>
#define FONT_NTILES 128
#define WALL_NGROUPS 16
#define ANIM_NCHARS 32
extern const unsigned char font_tiles[];
extern const unsigned char robbo_chars[];
extern const unsigned char wall_chars[];
extern const unsigned char ifnt_tiles[];
extern const unsigned char anim_a[];
extern const unsigned char anim_b[];
extern const unsigned char anim_slots[];
extern const unsigned char logo_tiles[];
BANKREF_EXTERN(gfx)
#define GFX_BANK BANK(gfx)   /* SWITCH_ROM(GFX_BANK) before reading the tiles */
#endif
