#include <gb/gb.h>
#include <gb/cgb.h>
#include "title.h"
#include "render.h"
#include "sound.h"
#include "gen/gfx_tiles.h"
#include "gen/instr.h"

/* Title: big 3D ROBBO logo + credits, and the original game's instruction text
   revealed with a vertical typewriter scroll - characters type out on the
   bottom row of a window and the window scrolls up one row per completed line,
   exactly as on the Atari.  The 27 logo tiles reuse the game-font VRAM region
   (0..26); the font is reloaded by game_run() afterwards. */
#define TXT_BASE  128
#define TPAL      0           /* white text on black */
#define LPAL      2           /* 3D blue logo */
#define SDIG      224         /* small single-row digits 224..233 */
#define SCOLON    234
#define SHYPHEN   235
#define CURSOR    236         /* typewriter caret */
#define SCOMMA    237
#define SPERIOD   238
#define SBANG     239
#define SQUEST    240
#define BLANK     0x40        /* the all-black playfield tile (screen fill) */
#define LOGO_BASE 0

/* instruction window: INSTR_WIDTH cols, NVIS rows, centered horizontally */
#define NVIS    8
#define BOXX    ((20 - INSTR_WIDTH) / 2)
#define BOXY    10
#define BOTROW  (BOXY + NVIS - 1)
#define CHAR_DELAY  2         /* frames per typed character */
#define HOLD_LINE  28         /* pause after a full line before scrolling */
#define HOLD_BLANK 12         /* pause on a blank (paragraph) line */

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

static void load_mono(unsigned char vram, const unsigned char *bits) {
    unsigned char t[16], r;
    for (r = 0; r < 8; r++) { t[r*2] = bits[r]; t[r*2+1] = bits[r]; }
    set_bkg_data(vram, 1, t);
}
static void bg_put(unsigned char x, unsigned char y, unsigned char tile, unsigned char pal) {
    set_bkg_tiles(x, y, 1, 1, &tile);
    set_bkg_attributes(x, y, 1, 1, &pal);
}
static unsigned char glyph(char c) {
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
static void bg_str(unsigned char x, unsigned char y, const char *s) {
    while (*s) { bg_put(x++, y, glyph(*s), TPAL); s++; }
}
static void letter3(unsigned char col, unsigned char row, unsigned char base) {
    unsigned char r, c;                          /* 3x3-tile logo letter */
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            bg_put(col + c, row + r, LOGO_BASE + base + r*3 + c, LPAL);
}

/* ---- instruction window helpers ---- */
static unsigned char line_len(unsigned char li) {
    unsigned char n = 0;
    if (li == 0xFF) return 0;
    while (n < INSTR_WIDTH && INSTR[li][n]) n++;
    return n;
}
static void draw_line(unsigned char row, unsigned char li) {
    unsigned char x = 0;
    if (li != 0xFF)
        for (; x < INSTR_WIDTH && INSTR[li][x]; x++)
            bg_put(BOXX + x, row, glyph(INSTR[li][x]), TPAL);
    for (; x < INSTR_WIDTH; x++) bg_put(BOXX + x, row, BLANK, TPAL);
}

/* short noise tick for the typewriter */
static void click(void) {
    NR41_REG = 0x00; NR42_REG = 0x51; NR43_REG = 0x30; NR44_REG = 0x80;
}

void title_show(void) {
    unsigned char x, y, d, r;
    unsigned char vis[NVIS];
    unsigned char li_next, col, curlen, state, delay, hold, blink, ps_on;
    snd_stop();                       /* silence any effect carried over from gameplay */
    const palette_color_t tpal[4] = { 0x0000, 0x294A, 0x56B5, 0x7FFF };
    const palette_color_t lpal[4] = { 0x0000, 0x3000, 0x7C00, 0x7FE0 };  /* shadow,blue,cyan */

    DISPLAY_OFF;
    HIDE_WIN;
    SCX_REG = 0; SCY_REG = 0;
    SWITCH_ROM(GFX_BANK);                          /* logo tiles live in a banked module */
    set_bkg_data(LOGO_BASE, 27, logo_tiles);
    for (d = 0; d < 10; d++) load_mono(SDIG + d, DIGITS[d]);
    load_mono(SCOLON, COLON);   load_mono(SHYPHEN, HYPHEN);
    load_mono(CURSOR, CARET);   load_mono(SCOMMA, COMMA);
    load_mono(SPERIOD, PERIOD); load_mono(SBANG, BANG);
    load_mono(SQUEST, QUEST);
    set_bkg_palette(TPAL, 1, tpal);
    set_bkg_palette(LPAL, 1, lpal);
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) bg_put(x, y, BLANK, TPAL);

    letter3(2,  0, 0);    /* R */
    letter3(5,  0, 9);    /* O */
    letter3(8,  0, 18);   /* B */
    letter3(11, 0, 18);   /* B */
    letter3(14, 0, 9);    /* O */

    bg_str(3, 4, "1989 BY AVALON");
    bg_str(5, 5, "JANUSZ PELC");
    bg_str(0, 7, "MOVE D-PAD  FIRE A-B");

    /* prime the typewriter window: all rows blank, first line at the bottom */
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

        if ((blink & 15) == 0) {                 /* blink PRESS START */
            ps_on ^= 1;
            if (ps_on) bg_str(4, 8, "PRESS START");
            else for (x = 0; x < 11; x++) bg_put(4 + x, 8, BLANK, TPAL);
        }

        if (state == 0) {                        /* TYPING */
            if (col < curlen) {                  /* draw/blink the caret */
                bg_put(BOXX + col, BOTROW, ((blink >> 3) & 1) ? CURSOR : BLANK, TPAL);
            }
            if (col >= curlen) {                 /* line finished */
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
        } else {                                 /* HOLD then scroll up one row */
            if (hold) {
                hold--;
            } else {
                for (r = 0; r < NVIS - 1; r++) vis[r] = vis[r + 1];
                for (r = 0; r < NVIS - 1; r++) draw_line(BOXY + r, vis[r]);
                draw_line(BOTROW, 0xFF);
                if (li_next >= INSTR_NLINES) li_next = 0;   /* loop the text */
                vis[NVIS - 1] = li_next++;
                col = 0; curlen = line_len(vis[NVIS - 1]);
                state = 0; delay = 0;
            }
        }

        if (joypad() & J_START) break;
    }
    waitpadup();
}
