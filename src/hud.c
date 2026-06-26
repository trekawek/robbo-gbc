/* GBC HUD for the gnu-robbo port: a window-layer status bar (bottom 2 rows)
   showing screws-left, keys, ammo and level.  Reads gnu-robbo's robbo/level_packs
   state (no lives/score - gnu-robbo restarts the level on death).

   This is a switchable-bank module: every function is __banked (a non-__banked
   function in a bank corrupts).  The I.FNT tiles + HUD palette are loaded by
   render_gr (which already holds GFX_BANK), so the HUD itself never SWITCH_ROMs. */
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

#define TXT_BASE 128
#define HUD_PAL  6                          /* palette 6 (boom merged into hazard) */
#define DTOP(n)  (TXT_BASE + 11 + (n))      /* I.FNT digit top half  */
#define DBOT(n)  (TXT_BASE + 1  + (n))      /* I.FNT digit bottom half */

static unsigned int c_scr;
static unsigned char c_key, c_amm, c_lvl, force;

void hwput(unsigned char x, unsigned char y, unsigned char tile) __banked {
    set_win_tiles(x, y, 1, 1, &tile);
    VBK_REG = 1; { unsigned char p = HUD_PAL; set_win_tiles(x, y, 1, 1, &p); } VBK_REG = 0;
}
void hwicon2(unsigned char x, unsigned char c0) __banked {
    hwput(x,   0, TXT_BASE + c0);     hwput(x+1, 0, TXT_BASE + c0 + 1);
    hwput(x,   1, TXT_BASE + c0 + 2); hwput(x+1, 1, TXT_BASE + c0 + 3);
}
void hwdig(unsigned char x, unsigned char n) __banked { hwput(x, 0, DTOP(n)); hwput(x, 1, DBOT(n)); }
void hwnum(unsigned char x, unsigned char v, unsigned char nd) __banked {
    unsigned char i;
    for (i = 0; i < nd; i++) { hwdig(x + (nd - 1 - i), v % 10); v /= 10; }
}

void hud_gr_init(void) __banked {
    unsigned char x;
    for (x = 0; x < 20; x++) { hwput(x, 0, 0x40); hwput(x, 1, 0x40); }   /* black bar */
    hwicon2(0,  77);   /* screws  - screw  */
    hwicon2(5,  85);   /* keys    - key    */
    hwicon2(10, 81);   /* ammo    - gun    */
    hwicon2(15, 69);   /* level   - planet */
    force = 1;
}

/* refresh counters; cheap/change-cached; call in VBlank */
void hud_gr_draw(void) __banked {
    unsigned char lvl = (unsigned char)level_packs[selected_pack].level_selected;
    if (force || (unsigned int)robbo.screws != c_scr) { hwnum(2,  (unsigned char)robbo.screws, 2); c_scr = robbo.screws; }
    if (force || (unsigned char)robbo.keys    != c_key) { hwnum(7,  (unsigned char)robbo.keys, 2);  c_key = robbo.keys; }
    if (force || (unsigned char)robbo.bullets != c_amm) { hwnum(12, (unsigned char)robbo.bullets, 2); c_amm = robbo.bullets; }
    if (force || lvl != c_lvl) { hwnum(17, lvl, 2); c_lvl = lvl; }
    force = 0;
}
