/* Ending sequence, ported from the Atari TITLE.ASM CONGR routine: Robbo walks
   in, a starship lands, Robbo waves, boards it, and it flies off; then an
   English congratulations text (the original is Polish; translated per request).

   Banked (like menu.c).  Graphics are the S.FNT metatiles converted to
   ending_tiles[] (loaded into BG tiles 0..ENDING_NTILES-1, over the playfield
   font); text uses the I.FNT font already at tile 128.  Scene palette (BG pal 0)
   uses the authentic PAL colours from COLS+6: black bg, brown ground, green
   ship/Robbo, white highlights. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "sound.h"
#include "gen/gfx_tiles.h"

void render_gr_init(void);

#define EPAL   0            /* scene BG palette                       */
#define BLANK  0x40         /* the all-black playfield font tile (64) */
#define TXT    128          /* I.FNT font base (ASCII 0x20 = tile 128) */
#define GROUND_ROW 15       /* ground occupies rows 15..16            */

/* 17 stars: Atari STAT (x 0..30, y 0..12) scaled to the 20x14 GBC sky. */
static const unsigned char STARS[17][2] = {
    {0,0},{16,0},{10,1},{18,1},{10,2},{15,3},{17,4},{3,4},{11,8},{15,8},
    {3,9},{7,10},{5,2},{8,5},{0,6},{1,12},{16,12}
};

/* draw a metatile (2x2 tiles) at tile (col,row); off = ending_tiles offset */
void draw_mt(unsigned char col, unsigned char row, unsigned char off) __banked {
    unsigned char t;
    t = off;     set_bkg_tiles(col,     row,     1, 1, &t);
    t = off + 1; set_bkg_tiles(col + 1, row,     1, 1, &t);
    t = off + 2; set_bkg_tiles(col,     row + 1, 1, 1, &t);
    t = off + 3; set_bkg_tiles(col + 1, row + 1, 1, 1, &t);
}

/* restore the static background (black sky, ground, or a star) at one tile */
void bg_cell(unsigned char col, unsigned char row) __banked {
    unsigned char t = BLANK, i;
    if (row >= GROUND_ROW) t = END_GROUND + ((col & 1)) + ((row - GROUND_ROW) ? 2 : 0);
    else
        for (i = 0; i < 17; i++)
            if (STARS[i][0] == col && STARS[i][1] == row) { t = END_STAR + 1; break; }
    set_bkg_tiles(col, row, 1, 1, &t);
}
/* erase a 2x2 metatile footprint back to background */
void erase_mt(unsigned char col, unsigned char row) __banked {
    bg_cell(col, row); bg_cell(col + 1, row);
    bg_cell(col, row + 1); bg_cell(col + 1, row + 1);
}

void ewait(unsigned char frames) __banked {
    while (frames--) wait_vbl_done();
}

/* ---- text ---- */
void etext(unsigned char col, unsigned char row, const char *s) __banked {
    unsigned char t;
    while (*s) { t = (unsigned char)(TXT + (*s - 0x20)); set_bkg_tiles(col++, row, 1, 1, &t); s++; }
}
void etext_c(unsigned char row, const char *s) __banked {  /* centred */
    unsigned char n = 0; const char *p = s;
    while (*p++) n++;
    etext((unsigned char)((20 - n) / 2), row, s);
}
void eclear(void) __banked {
    unsigned char x, y, t = BLANK;
    for (y = 0; y < 18; y++) for (x = 0; x < 20; x++) set_bkg_tiles(x, y, 1, 1, &t);
}

/* draw the whole static scene (black sky + stars + ground row) */
void draw_scene(void) __banked {
    unsigned char x, y, t = BLANK, i;
    for (y = 0; y < GROUND_ROW; y++) for (x = 0; x < 20; x++) set_bkg_tiles(x, y, 1, 1, &t);
    for (i = 0; i < 17; i++) { t = END_STAR + 1; set_bkg_tiles(STARS[i][0], STARS[i][1], 1, 1, &t); }
    for (x = 0; x < 20; x += 2) draw_mt(x, GROUND_ROW, END_GROUND);
}

/* draw the ship (2 metatiles wide = 4x2 tiles) at (col,row) */
void draw_ship(unsigned char col, unsigned char row) __banked {
    draw_mt(col, row, END_SHIPL);
    draw_mt(col + 2, row, END_SHIPR);
}
void erase_ship(unsigned char col, unsigned char row) __banked {
    erase_mt(col, row); erase_mt(col + 2, row);
}

/* ===================================================================== */
void ending_gr_show(void) __banked {
    const palette_color_t epal[4] = { 0x0000, 0x040A, 0x060A, 0x56B5 };
    unsigned char rc, sc, sr, i;

    snd_stop();
    DISPLAY_OFF;
    HIDE_WIN;
    SCX_REG = 0; SCY_REG = 0;
    /* ending_tiles were loaded into 0..ENDING_NTILES-1 by the HOME caller. */
    set_bkg_palette(EPAL, 1, epal);
    { unsigned char arow[20], y;                                 /* all tiles use pal 0 */
      for (y = 0; y < 20; y++) arow[y] = EPAL;
      VBK_REG = 1;
      for (y = 0; y < 18; y++) set_bkg_tiles(0, y, 20, 1, arow);
      VBK_REG = 0; }
    draw_scene();
    SHOW_BKG; DISPLAY_ON;

    /* Robbo walks in from the right (col 16 -> 8), on the ground (rows 13-14) */
    rc = 16;
    while (rc > 8) {
        draw_mt(rc, 13, (rc & 1) ? END_WALK1 : END_WALK2);
        ewait(7);
        erase_mt(rc, 13);
        rc--;
    }
    draw_mt(rc, 13, END_STAND);
    ewait(20);

    /* the ship descends (col 4) from the top to the ground */
    sc = 4;
    for (sr = 0; sr <= 13; sr++) {
        draw_ship(sc, sr);
        ewait(5);
        if (sr < 13) erase_ship(sc, sr);
    }
    ewait(15);

    /* Robbo waves 14 times (alternating wave frames) */
    for (i = 0; i < 14; i++) {
        draw_mt(rc, 13, (i & 1) ? END_WAVE1 : END_WAVE2);
        ewait(8);
    }
    draw_mt(rc, 13, END_STAND);
    ewait(15);

    /* Robbo walks left onto the ship (col 8 -> 5) */
    while (rc > 5) {
        erase_mt(rc, 13);
        rc--;
        draw_ship(sc, 13);                 /* keep the ship under him */
        draw_mt(rc, 13, (rc & 1) ? END_WALK1 : END_WALK2);
        ewait(8);
    }
    ewait(20);

    /* the ship (with Robbo aboard) flies up and off the top */
    for (sr = 13; sr != 0xFF; sr--) {
        draw_ship(sc, sr);
        draw_mt(rc, sr, END_STAND);
        ewait(5);
        erase_ship(sc, sr);
        erase_mt(rc, sr);
        if (sr == 0) break;
    }
    ewait(30);

    /* ---- congratulations text (English) ---- */
    {
        /* The I.FNT font at tile 128 has letters + digits but no punctuation, so
           the text is uppercase words only.  "|" marks a page break (hold+clear). */
        static const char *const PAGES[] = {
            "", "CONGRATULATIONS", "", "|",
            "ROBBO HAS BROKEN", "THROUGH THE ENEMY", "PLANETARY SYSTEM", "|",
            "THE PLANETS IN HIS", "MEMORY ARE VERY", "VALUABLE TO EARTH", "|",
            "YOU HAVE COMPLETED", "OUR FIRST GAME", "IF YOU LIKED IT", "LOOK FOR MORE", "|",
            "REMEMBER THE BEST", "GAMES COME ONLY FROM", "AVALON COMPUTER LAB", "|",
            "PRESS START",
        };
        unsigned char row = 4, k;
        eclear();
        for (k = 0; k < sizeof(PAGES) / sizeof(PAGES[0]); k++) {
            if (PAGES[k][0] == '|') {          /* page break: hold, then clear */
                ewait(150);
                eclear();
                row = 4;
            } else {
                if (PAGES[k][0]) etext_c(row, PAGES[k]);
                row += 2;
            }
        }
        /* PRESS START stays on screen until pressed */
        while (1) { wait_vbl_done(); if (joypad() & J_START) break; }
        waitpadup();
    }
}
