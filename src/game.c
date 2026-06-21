#include <gb/gb.h>
#include "game.h"
#include "cave.h"
#include "render.h"
#include "player.h"
#include "objects.h"
#include "hud.h"
#include "title.h"
#include "sound.h"
#include "levels.h"

GameState gs;
const signed char DX[4] = { 0, 0, -1, 1 };
const signed char DY[4] = { -1, 1, 0, 0 };

/* ---- dirty-cell queue ---- */
#define DIRTY_MAX 90
static unsigned char dx_[DIRTY_MAX], dy_[DIRTY_MAX];
static unsigned char ndirty;
static unsigned char full_redraw;

void mark_dirty(unsigned char x, unsigned char y) {
    if (full_redraw) return;
    if (ndirty >= DIRTY_MAX) { full_redraw = 1; return; }
    dx_[ndirty] = x; dy_[ndirty] = y; ndirty++;
}

static void flush_dirty(void) {
    unsigned char i;
    if (full_redraw) {
        render_repaint();
        full_redraw = 0; ndirty = 0;
        return;
    }
    for (i = 0; i < ndirty; i++) render_cell(dx_[i], dy_[i]);
    ndirty = 0;
}

void game_start_level(unsigned char idx) {
    const Level *l = level_ptr(idx);   /* switches to the planet's ROM bank */
    gs.level = idx;
    gs.robbo_x = l->robbo_x;
    gs.robbo_y = l->robbo_y;
    gs.facing = 1;
    gs.screws = l->screws;
    gs.keys = 0;
    gs.ammo = 0;                  /* the original clears ammo (NABO) on every level entry */
    gs.exit_open = (l->screws == 0);
    gs.dead = 0;
    gs.won = 0;

    DISPLAY_OFF;                 /* full redraw: VRAM freely writable, no tearing */
    cave_load(l);
    objects_reset();             /* clear stale beams/slides from the previous level */
    ndirty = 0; full_redraw = 0;
    render_load_level(l, idx);   /* sets camera + loads visible rows */
    hud_icons();
    hud_draw();
    SHOW_BKG; SHOW_WIN; DISPLAY_ON;
}

/* One logic step = one scan-order object sweep (Robbo is processed in place,
   in scan order, inside objects_update - matching the original CHNGCV). */
static void game_tick(unsigned char keys) {
    objects_update(keys);
}

/* ---- pause menu, drawn on the window layer over the playfield ---- */
#define MENU_PAL 7
static void mw(unsigned char x, unsigned char y, unsigned char tile) {
    set_win_tiles(x, y, 1, 1, &tile);
    VBK_REG = 1; { unsigned char p = MENU_PAL; set_win_tiles(x, y, 1, 1, &p); } VBK_REG = 0;
}
static void mw_str(unsigned char x, unsigned char y, const char *s) {
    while (*s) { unsigned char t = 128 + ((*s - 0x20) & 0x3F); mw(x++, y, t); s++; }
}
/* tall 8x16 I.FNT digit (top half row y, bottom half row y+1) */
static void mw_tdig(unsigned char x, unsigned char y, unsigned char n) {
    mw(x, y, 128 + 11 + n); mw(x, y + 1, 128 + 1 + n);
}
static void mw_score(unsigned char x, unsigned char y, unsigned long v) {
    unsigned char i;
    for (i = 0; i < 6; i++) { mw_tdig(x + 5 - i, y, (unsigned char)(v % 10)); v /= 10; }
}

/* right-pointing triangle cursor (I.FNT punctuation isn't usable) */
#define CURSOR_TILE 230
static const unsigned char CURSOR_BITS[8] = { 0x40,0x60,0x70,0x78,0x70,0x60,0x40,0x00 };

/* returns 0=resume, 1=restart level (lose a life), 2=warp, 3=quit to title */
static unsigned char pause_menu(void) {
    unsigned char sel = 0, x, y, keys, prev = 0xFF, ct[16];
    snd_stop();                       /* don't let an effect drone while paused */
    for (x = 0; x < 8; x++) { ct[x*2] = CURSOR_BITS[x]; ct[x*2+1] = CURSOR_BITS[x]; }
    VBK_REG = 0; set_bkg_data(CURSOR_TILE, 1, ct);
    for (y = 0; y < 18; y++) for (x = 0; x < 20; x++) mw(x, y, 0x40);  /* black */
    mw_str(7, 2, "PAUSED");
    mw_str(7, 5, "SCORE");
    mw_score(7, 6, gs.score);         /* 6 tall digits, rows 6-7 */
    mw_str(6, 10, "RESUME");
    mw_str(6, 12, "RESTART");
    mw_str(6, 14, "WARP");
    mw_str(6, 16, "QUIT");
    WY_REG = 0;                       /* window covers the whole screen */
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

/* test aid: pick any planet to jump to.  returns level index, 0xFF=cancel */
static unsigned char warp_menu(void) {
    unsigned char lvl = gs.level, keys, prev = 0xFF, x, y, disp;
    for (y = 0; y < 18; y++) for (x = 0; x < 20; x++) mw(x, y, 0x40);
    mw_str(6, 3, "WARP TO");
    mw_str(7, 5, "PLANET");
    mw_str(3, 13, "UP/DN  PICK");
    mw_str(3, 15, "A GO  B BACK");
    WY_REG = 0;
    waitpadup();
    while (1) {
        disp = lvl + 1;                                 /* show 1-based planet # */
        if (disp >= 10) mw_tdig(9, 8, disp / 10);
        else { mw(9, 8, 0x40); mw(9, 9, 0x40); }
        mw_tdig(10, 8, disp % 10);
        wait_vbl_done();
        keys = joypad();
        if ((keys & (J_UP | J_RIGHT)) && !(prev & (J_UP | J_RIGHT)))
            lvl = (unsigned char)((lvl + 1) % NLEVELS);
        if ((keys & (J_DOWN | J_LEFT)) && !(prev & (J_DOWN | J_LEFT)))
            lvl = (unsigned char)((lvl + NLEVELS - 1) % NLEVELS);
        if ((keys & (J_A | J_START)) && !(prev & (J_A | J_START))) { waitpadup(); return lvl; }
        if ((keys & J_B) && !(prev & J_B)) { waitpadup(); return 0xFF; }
        prev = keys;
    }
}

/* game-logic tick period, in real vblank frames.  The original steps every 7
   VBlanks; tying the tick to sys_time keeps that speed constant regardless of
   render framerate / CPU speed. */
#define TICK_FRAMES 7

/* On death: stamp an explosion where Robbo was and let the death sound play out
   before the level reloads (game_start_level blocks for many frames without
   pumping the sound, which otherwise swallows the effect). */
static void player_die(void) {
    unsigned char i;
    cave_set(gs.robbo_x, gs.robbo_y, 0x61);   /* 'a' = explosion frame */
    mark_dirty(gs.robbo_x, gs.robbo_y);
    snd_stop();                               /* the death effect always plays */
    snd_play(SND_DESTROY);
    for (i = 0; i < 36; i++) {
        wait_vbl_done();
        snd_update();
        flush_dirty();                        /* draw the explosion */
    }
}

void game_run(void) {
    unsigned char keys, prev = 0xFF, last_t;

    gs.lives = 3;
    gs.ammo = 0;
    gs.score = 0;
    render_init();               /* reload font: title overwrote tiles 0..26 with logo */
    game_start_level(0);         /* handles DISPLAY_OFF/ON + SHOW_BKG/WIN */
    last_t = (unsigned char)sys_time;

    while (1) {
        wait_vbl_done();
        snd_update();            /* advance the active sound effect one frame */
        hud_draw();              /* VBlank: cheap, change-cached HUD refresh */
        render_anim(gs.facing, gs.moving);
        flush_dirty();
        render_update(gs.robbo_x, gs.robbo_y);

        keys = joypad();

        if ((keys & J_START) && !(prev & J_START)) {     /* pause menu */
            unsigned char r = pause_menu();
            WY_REG = HUD_PX;                             /* restore HUD window */
            if (r == 1) {
                gs.dead = 1;                            /* restart level, lose a life */
            } else if (r == 2) {                        /* warp to a chosen planet */
                unsigned char w = warp_menu();
                WY_REG = HUD_PX;
                if (w != 0xFF) game_start_level(w);
                else { DISPLAY_OFF; hud_icons(); hud_draw(); DISPLAY_ON; }
            } else if (r == 3) {
                title_show();                           /* quit to title */
                gs.lives = 3; gs.ammo = 0; gs.score = 0;
                render_init();                          /* title wiped font 0..26 */
                game_start_level(0);
            } else {                                    /* resume: redraw wiped HUD icons */
                DISPLAY_OFF; hud_icons(); hud_draw(); DISPLAY_ON;
            }
            prev = keys;
            last_t = (unsigned char)sys_time;       /* menu ate time; don't burst-tick */
            continue;
        }

        if ((unsigned char)((unsigned char)sys_time - last_t) >= TICK_FRAMES) {
            last_t = (unsigned char)sys_time;
            game_tick(keys);
        }

        if (gs.won) {
            unsigned char n = gs.level + 1;
            if (n >= NLEVELS) n = 0;     /* wrap after the last of all 56 planets */
            game_start_level(n);
            last_t = (unsigned char)sys_time;
        }
        if (gs.dead) {
            if (gs.lives) gs.lives--;
            player_die();                       /* explosion + death sound */
            if (gs.lives == 0) { gs.lives = 3; gs.score = 0; }
            game_start_level(gs.level);
            last_t = (unsigned char)sys_time;
        }
        prev = keys;
    }
}
