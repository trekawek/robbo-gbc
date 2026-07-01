/* Title screen + pause/warp menu for the gnu-robbo port, backported from the
   original Atari robbo-gbc (src/title.c + src/game.c's menus).

   Banked module (every function is __banked, per the bank gotcha) to keep it out
   of the full HOME bank.  Uses the I.FNT text font already loaded at tile 128 by
   render_gr_init(); the logo tiles are loaded into 0..26 by render_gr_logo()
   (HOME) before title_gr_show() is entered, since a banked fn can't SWITCH_ROM
   to itself.  Instruction text is baked in gen/instr.h. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "levels_data.h"
#include "sound.h"
#include "gen/instr.h"
#include "gen/gfx_tiles.h"

void render_gr_init(void);
void hud_gr_init(void) __banked;

/* ===================================================================== */
/* ============================ TITLE SCREEN =========================== */
/* ===================================================================== */
#define TXT_BASE  128
#define TPAL      0           /* white text on black (palette 0) */
#define LPAL      2           /* 3D blue logo (palette 2)        */
#define SDIG      224         /* small single-row digits 224..233 */
#define SCOLON    234
#define SHYPHEN   235
#define CURSOR    236         /* typewriter caret */
#define SCOMMA    237
#define SPERIOD   238
#define SBANG     239
#define SQUEST    240
#define BLANK     0x40        /* the all-black playfield tile */
#define LOGO_X    ((20 - LOGO_TW) / 2)   /* centre the 14-tile-wide logo */

#define NVIS    8
#define BOXX    ((20 - INSTR_WIDTH) / 2)
#define BOXY    10
#define BOTROW  (BOXY + NVIS - 1)
#define CHAR_DELAY  2
#define HOLD_LINE  28
#define HOLD_BLANK 12

static const unsigned char DIGITS[10][8] = {
    {0,0x70,0x50,0x50,0x50,0x70,0,0},{0,0x20,0x60,0x20,0x20,0x70,0,0},
    {0,0x70,0x10,0x70,0x40,0x70,0,0},{0,0x70,0x10,0x30,0x10,0x70,0,0},
    {0,0x50,0x50,0x70,0x10,0x10,0,0},{0,0x70,0x40,0x70,0x10,0x70,0,0},
    {0,0x70,0x40,0x70,0x50,0x70,0,0},{0,0x70,0x10,0x20,0x20,0x20,0,0},
    {0,0x70,0x50,0x70,0x50,0x70,0,0},{0,0x70,0x50,0x70,0x10,0x70,0,0},
};
static const unsigned char COLON[8]   = {0,0x18,0x18,0,0x18,0x18,0,0};
static const unsigned char HYPHEN[8]  = {0,0,0,0x7E,0x7E,0,0,0};
static const unsigned char CARET[8]   = {0,0,0,0,0,0,0,0x7E};
static const unsigned char COMMA[8]   = {0,0,0,0,0,0x30,0x30,0x60};
static const unsigned char PERIOD[8]  = {0,0,0,0,0,0,0x60,0x60};
static const unsigned char BANG[8]    = {0x20,0x20,0x20,0x20,0x20,0,0x20,0};
static const unsigned char QUEST[8]   = {0x70,0x10,0x20,0x20,0,0x20,0,0};

void load_mono(unsigned char vram, const unsigned char *bits) __banked {
    unsigned char t[16], r;
    for (r = 0; r < 8; r++) { t[r*2] = bits[r]; t[r*2+1] = bits[r]; }
    set_bkg_data(vram, 1, t);
}
void bg_put(unsigned char x, unsigned char y, unsigned char tile, unsigned char pal) __banked {
    set_bkg_tiles(x, y, 1, 1, &tile);
    set_bkg_attributes(x, y, 1, 1, &pal);
}
unsigned char glyph(char c) __banked {
    if (c >= '0' && c <= '9') return SDIG + (c - '0');
    switch (c) {
        case ' ': return BLANK;
        case ':': return SCOLON;
        case '-': return SHYPHEN;
        case ',': return SCOMMA;
        case '.': return SPERIOD;
        case '!': return SBANG;
        case '?': return SQUEST;
    }
    return TXT_BASE + (((unsigned char)c - 0x20) & 0x3F);
}
void bg_str(unsigned char x, unsigned char y, const char *s) __banked {
    while (*s) { bg_put(x++, y, glyph(*s), TPAL); s++; }
}
unsigned char line_len(unsigned char li) __banked {
    unsigned char n = 0;
    if (li == 0xFF) return 0;
    while (n < INSTR_WIDTH && INSTR[li][n]) n++;
    return n;
}
void draw_line(unsigned char row, unsigned char li) __banked {
    unsigned char x = 0;
    if (li != 0xFF)
        for (; x < INSTR_WIDTH && INSTR[li][x]; x++)
            bg_put(BOXX + x, row, glyph(INSTR[li][x]), TPAL);
    for (; x < INSTR_WIDTH; x++) bg_put(BOXX + x, row, BLANK, TPAL);
}
void click(void) __banked {
    NR41_REG = 0x00; NR42_REG = 0x51; NR43_REG = 0x30; NR44_REG = 0x80;
}

extern unsigned int gr_logo_rainbow[LOGO_RAINBOW_N][4];

void title_gr_show(void) __banked {
    unsigned char x, y, d, r;
    unsigned char vis[NVIS];
    unsigned char li_next, col, curlen, state, delay, hold, blink, ps_on, hue;
    const palette_color_t tpal[4] = { 0x0000, 0x294A, 0x56B5, 0x7FFF };

    snd_stop();
    DISPLAY_OFF;
    HIDE_WIN;
    SCX_REG = 0; SCY_REG = 0;
    /* logo tiles are loaded into 0..26 by render_gr_logo() before this call */
    for (d = 0; d < 10; d++) load_mono(SDIG + d, DIGITS[d]);
    load_mono(SCOLON, COLON);   load_mono(SHYPHEN, HYPHEN);
    load_mono(CURSOR, CARET);   load_mono(SCOMMA, COMMA);
    load_mono(SPERIOD, PERIOD); load_mono(SBANG, BANG);
    load_mono(SQUEST, QUEST);
    set_bkg_palette(TPAL, 1, tpal);
    hue = 0;
    set_bkg_palette(LPAL, 1, (const palette_color_t *)gr_logo_rainbow[0]);
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) bg_put(x, y, BLANK, TPAL);

    /* authentic 'RoDDo' logo: LOGO_TW x LOGO_TH tiles (0..LOGO_NTILES-1),
       centred horizontally, drawn in the LPAL palette that the rainbow cycles. */
    for (r = 0; r < LOGO_TH; r++)
        for (x = 0; x < LOGO_TW; x++)
            bg_put(LOGO_X + x, r, (unsigned char)(r * LOGO_TW + x), LPAL);

    bg_str(3, 4, "1989 BY AVALON");
    bg_str(5, 5, "JANUSZ PELC");
    bg_str(0, 7, "MOVE D-PAD  FIRE A-B");

    for (r = 0; r < NVIS; r++) { vis[r] = 0xFF; draw_line(BOXY + r, 0xFF); }
    li_next = 0;
    vis[NVIS - 1] = li_next++;
    col = 0; curlen = line_len(vis[NVIS - 1]);
    state = 0; delay = 0; hold = 0; blink = 0; ps_on = 1;
    bg_str(4, 8, "PRESS START");

    SHOW_BKG; DISPLAY_ON;
    while (1) {
        wait_vbl_done();
        blink++;
        /* rainbow: advance the logo hue every 8 frames (~2s per full cycle) */
        if ((blink & 7) == 0) {
            if (++hue >= LOGO_RAINBOW_N) hue = 0;
            set_bkg_palette(LPAL, 1, (const palette_color_t *)gr_logo_rainbow[hue]);
        }
        if ((blink & 15) == 0) {
            ps_on ^= 1;
            if (ps_on) bg_str(4, 8, "PRESS START");
            else for (x = 0; x < 11; x++) bg_put(4 + x, 8, BLANK, TPAL);
        }
        if (state == 0) {
            if (col < curlen)
                bg_put(BOXX + col, BOTROW, ((blink >> 3) & 1) ? CURSOR : BLANK, TPAL);
            if (col >= curlen) {
                if (col < INSTR_WIDTH) bg_put(BOXX + col, BOTROW, BLANK, TPAL);
                state = 1; hold = curlen ? HOLD_LINE : HOLD_BLANK;
            } else if (delay) {
                delay--;
            } else {
                char c = INSTR[vis[NVIS - 1]][col];
                bg_put(BOXX + col, BOTROW, glyph(c), TPAL);
                if (c != ' ') click();
                col++; delay = CHAR_DELAY;
            }
        } else {
            if (hold) {
                hold--;
            } else {
                for (r = 0; r < NVIS - 1; r++) vis[r] = vis[r + 1];
                for (r = 0; r < NVIS - 1; r++) draw_line(BOXY + r, vis[r]);
                draw_line(BOTROW, 0xFF);
                if (li_next >= INSTR_NLINES) li_next = 0;
                vis[NVIS - 1] = li_next++;
                col = 0; curlen = line_len(vis[NVIS - 1]);
                state = 0; delay = 0;
            }
        }
        if (joypad() & J_START) break;
    }
    waitpadup();
}

/* ===================================================================== */
/* ============================ PAUSE / WARP =========================== */
/* ===================================================================== */
#define MENU_PAL 7
void mw(unsigned char x, unsigned char y, unsigned char tile) __banked {
    set_win_tiles(x, y, 1, 1, &tile);
    VBK_REG = 1; { unsigned char p = MENU_PAL; set_win_tiles(x, y, 1, 1, &p); } VBK_REG = 0;
}
void mw_str(unsigned char x, unsigned char y, const char *s) __banked {
    /* I.FNT at tile 128 has no punctuation glyphs (only the SHYPHEN/etc. tiles
       do), so map '-' to its dedicated tile like glyph() does for bg_str. */
    while (*s) {
        unsigned char t = (*s == '-') ? SHYPHEN : 128 + ((*s - 0x20) & 0x3F);
        mw(x++, y, t); s++;
    }
}
void mw_tdig(unsigned char x, unsigned char y, unsigned char n) __banked {
    mw(x, y, 128 + 11 + n); mw(x, y + 1, 128 + 1 + n);
}
void mw_score(unsigned char x, unsigned char y, unsigned long v) __banked {
    unsigned char i;
    for (i = 0; i < 6; i++) { mw_tdig(x + 5 - i, y, (unsigned char)(v % 10)); v /= 10; }
}

#define CURSOR_TILE 230
static const unsigned char CURSOR_BITS[8] = { 0x40,0x60,0x70,0x78,0x70,0x60,0x40,0x00 };

/* set palette 7 (white-on-black) for the menu; render_gr_load doesn't touch it */
void menu_palette(void) __banked {
    const palette_color_t mp[4] = { 0x0000, 0x7FFF, 0x294A, 0x56B5 };
    set_bkg_palette(MENU_PAL, 1, mp);
}

/* 0=resume, 1=restart level, 2=warp, 3=quit to title */
unsigned char pause_gr(void) __banked {
    unsigned char sel = 0, x, y, keys, prev = 0xFF, ct[16];
    snd_stop();
    menu_palette();
    for (x = 0; x < 8; x++) { ct[x*2] = CURSOR_BITS[x]; ct[x*2+1] = CURSOR_BITS[x]; }
    VBK_REG = 0; set_bkg_data(CURSOR_TILE, 1, ct);
    for (y = 0; y < 18; y++) for (x = 0; x < 20; x++) mw(x, y, 0x40);
    mw_str(7, 2, "PAUSED");
    mw_str(7, 5, "SCORE");
    mw_score(7, 6, gr_score);
    mw_str(6, 10, "RESUME");
    mw_str(6, 12, "RESTART");
    mw_str(6, 14, "WARP");
    mw_str(6, 16, "QUIT");
    WX_REG = 7; WY_REG = 0;            /* window covers the whole screen */
    SHOW_WIN;
    waitpadup();
    while (1) {
        for (y = 10; y <= 16; y += 2) mw(4, y, 0x40);
        mw(4, 10 + sel * 2, CURSOR_TILE);
        wait_vbl_done();
        keys = joypad();
        if ((keys & J_UP)   && !(prev & J_UP)   && sel)     sel--;
        if ((keys & J_DOWN) && !(prev & J_DOWN) && sel < 3) sel++;
        if ((keys & (J_A | J_START)) && !(prev & (J_A | J_START))) { waitpadup(); return sel; }
        prev = keys;
    }
}

/* pick any level to jump to; returns 0-based level index, 0xFF=cancel */
unsigned char warp_gr(void) __banked {
    unsigned char lvl, keys, prev = 0xFF, x, y, disp;
    lvl = (unsigned char)(level_packs[selected_pack].level_selected - 1);
    for (y = 0; y < 18; y++) for (x = 0; x < 20; x++) mw(x, y, 0x40);
    mw_str(6, 3, "WARP TO");
    mw_str(7, 5, "LEVEL");
    mw_str(3, 13, "D-PAD PICK");
    mw_str(3, 15, "A GO  B BACK");
    WX_REG = 7; WY_REG = 0;
    waitpadup();
    while (1) {
        disp = lvl + 1;                          /* 1-based level # */
        if (disp >= 10) mw_tdig(9, 8, disp / 10);
        else { mw(9, 8, 0x40); mw(9, 9, 0x40); }
        mw_tdig(10, 8, disp % 10);
        wait_vbl_done();
        keys = joypad();
        if ((keys & (J_UP | J_RIGHT)) && !(prev & (J_UP | J_RIGHT)))
            lvl = (unsigned char)((lvl + 1) % GR_NLEVELS);
        if ((keys & (J_DOWN | J_LEFT)) && !(prev & (J_DOWN | J_LEFT)))
            lvl = (unsigned char)((lvl + GR_NLEVELS - 1) % GR_NLEVELS);
        if ((keys & (J_A | J_START)) && !(prev & (J_A | J_START))) { waitpadup(); return lvl; }
        if ((keys & J_B) && !(prev & J_B)) { waitpadup(); return 0xFF; }
        prev = keys;
    }
}
