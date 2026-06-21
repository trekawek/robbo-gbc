#include <gb/gb.h>
#include <gb/cgb.h>
#include "hud.h"
#include "game.h"
#include "object_tables.h"
#include "gen/gfx_tiles.h"

/* Black status bar: 1-col icon + 2 tall-digit count, spaced into groups:
   screws  lives  keys  bullets  planet.  Drawn into the window layer.
   hud_draw() is cheap (change-cached) and MUST run inside VBlank. */
#define TXT_BASE 128
#define HUD_PAL  7
#define DTOP(n)  (TXT_BASE + 11 + (n))    /* I.FNT digit top half  */
#define DBOT(n)  (TXT_BASE + 1  + (n))    /* I.FNT digit bottom half */

static unsigned int  c_scr;
static unsigned char c_liv, c_key, c_amm, c_lvl, force;

static void wput(unsigned char x, unsigned char y, unsigned char tile) {
    set_win_tiles(x, y, 1, 1, &tile);
    VBK_REG = 1; { unsigned char p = HUD_PAL; set_win_tiles(x, y, 1, 1, &p); } VBK_REG = 0;
}
/* full 16x16 I.FNT status icon = 4 consecutive chars c0,c0+1 / c0+2,c0+3 */
static void wicon2(unsigned char x, unsigned char c0) {
    wput(x,   0, TXT_BASE + c0);     wput(x+1, 0, TXT_BASE + c0 + 1);
    wput(x,   1, TXT_BASE + c0 + 2); wput(x+1, 1, TXT_BASE + c0 + 3);
}
static void wdig(unsigned char x, unsigned char n) { wput(x, 0, DTOP(n)); wput(x, 1, DBOT(n)); }
static void wnum(unsigned char x, unsigned char v, unsigned char nd) {
    unsigned char i;
    for (i = 0; i < nd; i++) { wdig(x + (nd - 1 - i), v % 10); v /= 10; }
}

/* (re)draw the static icons + black strip; forces the next hud_draw to repaint */
/* Original status-bar icons from I.FNT (white outlines), full 16x16. The icon
   chars (verified against the rendered glyphs): 77=screw, 73=creature, 85=key,
   81=gun, 69=planet - matching the original HUD order. */
void hud_icons(void) {
    unsigned char x;
    for (x = 0; x < 20; x++) { wput(x, 0, 0x40); wput(x, 1, 0x40); }
    wicon2(0,  77);   /* screws  - screw    */
    wicon2(4,  73);   /* lives   - creature */
    wicon2(8,  85);   /* keys    - key      */
    wicon2(12, 81);   /* bullets - gun      */
    wicon2(16, 69);   /* planet  - planet   */
    force = 1;
}

void hud_init(void) {
    const palette_color_t pal[4] = { 0x0000, 0x7FFF, 0x167A, 0x7FFF };
    SWITCH_ROM(GFX_BANK);                          /* ifnt tiles live in a banked module */
    set_bkg_data(TXT_BASE, 96, ifnt_tiles);
    set_bkg_palette(HUD_PAL, 1, pal);
    hud_icons();
}

void hud_draw(void) {
    if (force || gs.screws != c_scr) { wnum(2,  (unsigned char)gs.screws, 2); c_scr = gs.screws; }
    if (force || gs.lives  != c_liv) { wnum(6,  gs.lives, 2);     c_liv = gs.lives; }
    if (force || gs.keys   != c_key) { wnum(10, gs.keys, 2);      c_key = gs.keys; }
    if (force || gs.ammo   != c_amm) { wnum(14, gs.ammo, 2);      c_amm = gs.ammo; }
    if (force || gs.level  != c_lvl) { wnum(18, gs.level + 1, 2); c_lvl = gs.level; }
    force = 0;
}
