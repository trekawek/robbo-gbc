/* Atari TITLE.ASM CONGR, scaled to the GBC screen. The original WAIT falls
   through to HALT, and DISP's scanline-synchronized redraw adds another frame:
   WAIT 6 scene poses are visibly eight PAL frames apart in the reference video.
   Keep the original poses, sound cues, patterned fade and random text dissolve. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "sound.h"
#include "gen/gfx_tiles.h"

#define BLANK 0x40
#define PATTERN_TILE 245
#define WIPE_TILE 246
#define TEXT_BANG 247
#define TEXT_PERIOD 248
#define TEXT_COMMA 249
#define TEXT_COLON 250
#define GROUND_ROW 16
#define FEET_ROW 14
#define SHIP_COL 8
#define HIDDEN 255
#define POSE_FRAMES 8

static const unsigned char STARS[17][2] = {
    {0,0},{16,0},{10,1},{18,1},{10,2},{15,3},{17,4},{3,4},{11,8},{15,8},
    {3,9},{7,10},{5,2},{8,5},{0,6},{1,12},{16,12}
};
static const unsigned char pattern_rows[8] = END_PATTERN_ROWS;
static const palette_color_t fade_colors[15] = END_TEXT_FADE_COLORS;
static const palette_color_t scene_palettes[8] = {
    END_COLOR_BG, END_COLOR_PF0, END_COLOR_PF1, END_COLOR_PF2,
    END_COLOR_BG, END_COLOR_PF0, END_COLOR_PF1, END_COLOR_PF3
};
/* Each page has an 18-column interior. The second keeps Avalon's original
   publisher message; START advances pages before the final closing wipe. */
static const char message[2][18][21] = {
    {
        "~~~~~~~~~~~~~~~~~~~~",
        "~                  ~",
        "~                  ~",
        "~    WELL DONE!    ~",
        "~                  ~",
        "~ROBBO HAS ESCAPED ~",
        "~   THE HOSTILE    ~",
        "~PLANETARY SYSTEM. ~",
        "~                  ~",
        "~ THE PLANS STORED ~",
        "~IN HIS MEMORY ARE ~",
        "~OF GREAT VALUE TO ~",
        "~      EARTH!      ~",
        "~                  ~",
        "~                  ~",
        "~START TO CONTINUE ~",
        "~                  ~",
        "~~~~~~~~~~~~~~~~~~~~"
    },
    {
        "~~~~~~~~~~~~~~~~~~~~",
        "~                  ~",
        "~YOU HAVE COMPLETED~",
        "~ OUR FIRST GAME.  ~",
        "~IF YOU ENJOYED IT,~",
        "~ LOOK OUT FOR OUR ~",
        "~  NEXT RELEASES.  ~",
        "~                  ~",
        "~    REMEMBER:     ~",
        "~  THE BEST GAMES  ~",
        "~    COME FROM     ~",
        "~     AVALON!      ~",
        "~                  ~",
        "~                  ~",
        "~                  ~",
        "~   PRESS START    ~",
        "~                  ~",
        "~~~~~~~~~~~~~~~~~~~~"
    }
};

/* I.FNT's punctuation positions hold Atari icons. Supply the same marks as
   the title font explicitly, in four otherwise unused ending tiles. */
static const unsigned char text_punctuation[64] = {
    0x20,0x20, 0x20,0x20, 0x20,0x20, 0x20,0x20,
    0x20,0x20, 0,0, 0x20,0x20, 0,0,                 /* ! */
    0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0x60,0x60, 0x60,0x60, /* . */
    0,0, 0,0, 0,0, 0,0, 0,0, 0x30,0x30, 0x30,0x30, 0x60,0x60, /* , */
    0,0, 0x18,0x18, 0x18,0x18, 0,0,
    0,0, 0x18,0x18, 0x18,0x18, 0,0                /* : */
};

static unsigned int frame_deadline, pal_fraction, reveal_rng;
static unsigned char pal_frame, pattern_active, ship_row, text_page;

void pattern_draw(void) __banked {
    unsigned char tile[16], row, bits;
    for (row = 0; row < 8; row++) {
        bits = pattern_rows[(row - pal_frame) & 7];
        tile[row * 2] = tile[row * 2 + 1] = bits;
    }
    set_bkg_data(PATTERN_TILE, 1, tile);
}

void ending_clock_reset(void) __banked {
    __critical { frame_deadline = sys_time; }
    pal_fraction = 0;
    pal_frame = 0;
}

/* One PAL frame is 3287/2744 GBC frames. An absolute deadline includes drawing
   time in each interval; it cannot accumulate an extra frame per pose. */
void ewait(unsigned char frames) __banked {
    unsigned int now;
    while (frames--) {
        frame_deadline++;
        pal_fraction += 543;
        if (pal_fraction >= 2744) { pal_fraction -= 2744; frame_deadline++; }
        while (1) {
            __critical { now = sys_time; }
            if ((int)(now - frame_deadline) >= 0) break;
            wait_vbl_done();
        }
        pal_frame++;
        if (pattern_active) pattern_draw();
    }
}

void draw_mt(unsigned char col, unsigned char row, unsigned char off,
             unsigned char palette) __banked {
    unsigned char tiles[4], attrs[4], i;
    for (i = 0; i < 4; i++) { tiles[i] = off + i; attrs[i] = palette; }
    set_bkg_tiles(col, row, 2, 2, tiles);
    set_bkg_attributes(col, row, 2, 2, attrs);
}

void bg_cell(unsigned char col, unsigned char row) __banked {
    unsigned char tile = BLANK, palette = 0, i;
    if (row >= GROUND_ROW) {
        tile = END_GROUND + (col & 1) + ((row - GROUND_ROW) ? 2 : 0);
        palette = 1; /* Atari ground uses the inverse glyph. */
    } else {
        for (i = 0; i < 17; i++)
            if (STARS[i][0] == col && STARS[i][1] == row) { tile = END_STAR + 1; break; }
    }
    set_bkg_tiles(col, row, 1, 1, &tile);
    set_bkg_attributes(col, row, 1, 1, &palette);
}

void draw_scene(void) __banked {
    unsigned char tiles[20], attrs[20], x, y;
    for (x = 0; x < 20; x++) { tiles[x] = BLANK; attrs[x] = 0; }
    for (y = 0; y < 18; y++) {
        set_bkg_tiles(0, y, 20, 1, tiles);
        set_bkg_attributes(0, y, 20, 1, attrs);
    }
    for (x = 0; x < 17; x++) bg_cell(STARS[x][0], STARS[x][1]);
    for (x = 0; x < 20; x += 2) draw_mt(x, GROUND_ROW, END_GROUND, 1);
}

void robbo_hide(void) __banked {
    unsigned char i;
    for (i = 0; i < 4; i++) move_sprite(i, 0, 0);
}

/* Pixel positions retain all nine walking poses on the narrower GBC screen. */
void robbo_at(unsigned char x, unsigned char frame) __banked {
    unsigned char i, sx;
    if (x == HIDDEN) { robbo_hide(); return; }
    for (i = 0; i < 4; i++) {
        sx = x + ((i & 1) ? 8 : 0);
        set_sprite_tile(i, frame + i);
        set_sprite_prop(i, S_PRIORITY);
        /* The original draws the ship over Robbo as he boards it. */
        if (ship_row == FEET_ROW && sx + 8 <= (SHIP_COL + 4) * 8 && sx >= SHIP_COL * 8)
            move_sprite(i, 0, 0);
        else move_sprite(i, sx + 8, FEET_ROW * 8 + 16 + ((i & 2) ? 8 : 0));
    }
}

void scene_pose(unsigned char x, unsigned char frame, signed char atari_ship_y) __banked {
    unsigned char col, row, palette;
    ewait(POSE_FRAMES);
    if (ship_row != HIDDEN)
        for (row = ship_row; row < ship_row + 2; row++)
            for (col = SHIP_COL; col < SHIP_COL + 4; col++) bg_cell(col, row);
    ship_row = atari_ship_y < 0 ? HIDDEN : (unsigned char)atari_ship_y * 7 / 6;
    robbo_at(x, frame);
    if (ship_row != HIDDEN) {
        palette = (pal_frame & 16) ? 1 : 0;
        draw_mt(SHIP_COL, ship_row, END_SHIPL, palette);
        draw_mt(SHIP_COL + 2, ship_row, END_SHIPR, palette);
    }
}

void text_palette(palette_color_t foreground, palette_color_t background) __banked {
    palette_color_t normal[4], inverse[4];
    unsigned char i;
    for (i = 0; i < 3; i++) { normal[i] = background; inverse[i] = foreground; }
    normal[3] = foreground; inverse[3] = background;
    set_bkg_palette(0, 1, normal);
    set_bkg_palette(2, 1, inverse);
}

void text_cell(unsigned char x, unsigned char y) __banked {
    unsigned char c = message[text_page][y][x], tile, palette;
    switch (c) {
        case '~': tile = PATTERN_TILE; break;
        case '!': tile = TEXT_BANG; break;
        case '.': tile = TEXT_PERIOD; break;
        case ',': tile = TEXT_COMMA; break;
        case ':': tile = TEXT_COLON; break;
        default: tile = 128 + c - 32; break;
    }
    palette = y == (text_page ? 11 : 3) && c != ' ' && c != '~' ? 2 : 0;
    set_bkg_tiles(x, y, 1, 1, &tile);
    set_bkg_attributes(x, y, 1, 1, &palette);
}

/* The original chooses three random cells across its 768-byte text buffer per
   busy loop. Fourteen cells per PAL frame preserves its roughly three-second
   dissolve after scaling the screen to 360 cells. This RNG is local so a
   pause-menu preview cannot alter the game's future random events. */
void reveal_cells(void) __banked {
    unsigned char i;
    unsigned int position;
    for (i = 0; i < 14; i++) {
        reveal_rng ^= reveal_rng << 7;
        reveal_rng ^= reveal_rng >> 9;
        reveal_rng ^= reveal_rng << 3;
        position = reveal_rng % 360;
        text_cell(position % 20, position / 20);
    }
}

/* A bright band rises from the bottom, like Atari's ZNIK raster wipe. The
   window gives us pixel movement without changing the interrupt handlers. */
void text_wipe(void) __banked {
    static const unsigned char band[16] = {
        255,255, 255,255, 0,255, 0,255, 255,0, 255,0, 0,0, 0,0
    };
    palette_color_t palette[4] = {0, END_TEXT_BG, END_COLOR_PF2, 0x7FFF};
    unsigned char tiles[20], attrs[20], x, y;
    unsigned int step;
    pattern_active = 0;
    DISPLAY_OFF;
    set_bkg_data(WIPE_TILE, 1, band);
    set_bkg_palette(3, 1, palette);
    for (x = 0; x < 20; x++) { tiles[x] = BLANK; attrs[x] = 3; }
    for (y = 0; y < 18; y++) {
        set_win_tiles(0, y, 20, 1, tiles);
        VBK_REG = 1; set_win_tiles(0, y, 20, 1, attrs); VBK_REG = 0;
    }
    for (x = 0; x < 20; x++) tiles[x] = WIPE_TILE;
    set_win_tiles(0, 0, 20, 1, tiles);
    WX_REG = 7; WY_REG = 144; SHOW_WIN;
    DISPLAY_ON;
    ending_clock_reset();
    snd_play(9);
    for (step = 1; step <= 117; step++) {
        ewait(1);
        WY_REG = 144 - (unsigned char)(step * 144 / 117);
    }
    /* Clear the bright edge once it has crossed the top of the screen. */
    for (x = 0; x < 20; x++) tiles[x] = BLANK;
    set_win_tiles(0, 0, 20, 1, tiles);
    WY_REG = 0;
    snd_play(13);
}

void ending_text(void) __banked {
    unsigned char tiles[20], attrs[20], x, y;
    DISPLAY_OFF;
    HIDE_SPRITES;
    set_bkg_data(TEXT_BANG, 4, text_punctuation);
    for (text_page = 0; text_page < 2; text_page++) {
        DISPLAY_OFF;
        text_palette(0, 0);
        pattern_active = 1;
        pattern_draw();
        for (x = 0; x < 20; x++) { tiles[x] = PATTERN_TILE; attrs[x] = 0; }
        for (y = 0; y < 18; y++) {
            set_bkg_tiles(0, y, 20, 1, tiles);
            set_bkg_attributes(0, y, 20, 1, attrs);
        }
        DISPLAY_ON;
        ending_clock_reset();
        for (y = 0; y < 15; y++) { text_palette(fade_colors[y], 0); ewait(3); }
        text_palette(fade_colors[14], END_TEXT_BG);
        if (!text_page) snd_play(0);
        reveal_rng = 0xACE1;
        do { reveal_cells(); ewait(1); } while (!(joypad() & J_START));
        /* One press advances one page, even when START is held down. */
        waitpadup();
    }
    text_wipe();
    DISPLAY_OFF;
    HIDE_WIN;
}

void ending_gr_show(void) __banked {
    unsigned char i;
    snd_stop();
    DISPLAY_OFF;
    HIDE_WIN;
    SCX_REG = 0; SCY_REG = 0;
    VBK_REG = 0;
    set_bkg_palette(0, 2, scene_palettes);
    set_sprite_palette(0, 1, scene_palettes);
    draw_scene();
    SPRITES_8x8;
    robbo_hide();
    ship_row = HIDDEN;
    pattern_active = 0;
    SHOW_BKG; SHOW_SPRITES; DISPLAY_ON;
    ending_clock_reset();
    ewait(51);

    snd_play(5);
    for (i = 0; i < 9; i++)
        scene_pose(144 - i * 4, (i & 1) ? END_WALK1 : END_WALK2, -1);
    scene_pose(108, END_STAND, -1);
    ewait(11);

    snd_play(14);
    for (i = 0; i < 12; i++)
        scene_pose(108, (i & 1) ? END_STAND2 : END_STAND, i);

    snd_play(13);
    for (i = 0; i < 14; i++) {
        scene_pose(108, END_WAVE2, 12);
        scene_pose(108, END_WAVE1, 12);
    }

    for (i = 0; i < 9; i++)
        scene_pose(108 - i * 4, (i & 1) ? END_WALK1 : END_WALK2, 12);

    snd_play(11);
    for (i = 0; i < 14; i++) scene_pose(HIDDEN, END_STAND, 12 - (signed char)i);
    snd_play(5);
    ewait(51);
    ending_text();
}
