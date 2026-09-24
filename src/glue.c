/* GBC platform shim for the gnu-robbo logic (board.c): RNG, sound bridge,
   main loop, input, level/death lifecycle. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"

/* PROF: profiling bitmask - skip components to measure their per-cycle cost via
   Perf (cycle rate).  0 = normal build.  1=update_game 2=show_game_area+hud
   4=render_gr_anim 16=render_gr_camera */
#ifndef PROF
#define PROF 0
#endif
#include "levels_data.h"
#include "sound.h"
#include "render.h"

void hud_gr_init(void) __banked;
void hud_gr_draw(void) __banked;
void render_gr_logo(void);
void render_gr_ending(void);
void ending_gr_show(void) __banked;
void title_gr_show(void) __banked;
unsigned char pause_gr(void) __banked;
unsigned char warp_gr(void) __banked;

/* ---- RNG: board.c uses rand() (low bits) and my_rand() ---- */
static unsigned int rng = 0xACE1u;
int rand(void) {
    rng ^= rng << 7; rng ^= rng >> 9; rng ^= rng << 3;
    return (int)(rng & 0x7FFF);
}
int my_rand(void) { return rand(); }
void my_srand(unsigned int seed) { if (seed) rng = seed; }
int abs(int v) { return v < 0 ? -v : v; }

/* ---- sound: bridge gnu-robbo SFX_* events to the GB sound engine ---- */
static const unsigned char SFX2SND[] = {
    0xFF,         /* 0  unused      */
    SND_AMMO,     /* 1  SFX_BULLET (ammo pickup) */
    SND_SCREW,    /* 2  SFX_SCREW    */
    SND_EXPLODE,  /* 3  SFX_BOMB     */
    SND_PUSH,     /* 4  SFX_BOX      */
    SND_DOOR,     /* 5  SFX_DOOR     */
    SND_SHOOT,    /* 6  SFX_GUN      */
    SND_KEY,      /* 7  SFX_KEY      */
    SND_SHOOT,    /* 8  SFX_SHOOT    */
    SND_SHOOT,    /* 9  SFX_BIRD     */
    SND_TELEPORT, /* 10 SFX_TELEPORT */
    0xFF,         /* 11 SFX_ROBBO: walking is silent on Atari */
    SND_WIN,      /* 12 SFX_CAPSULE (level done) */
    SND_DESTROY,  /* 13 SFX_KILL     */
    SND_MAGNET,   /* 14 SFX_MAGNET   */
    SND_CAPSULE,  /* 15 SFX_EXIT_OPEN */
    SND_KNOCK,    /* 16 SFX_KNOCK (bullet/laser hits a wall) */
    SND_LIFE,     /* 17 SFX_BONUS (score bonus glyph) */
};
void play_sound(int event, int vol) {
    /* The engine emits world-event sounds (gun/bird/bomb/kill/box) as SND_NORM
       when in_viewport() and SND_QUIET when off-screen.  in_viewport now tracks
       the real GBC camera window (render.c set_sound_viewport), so drop the
       off-screen ones - otherwise distant guns/birds fire a constant, sourceless
       crackle.  Robbo's own actions always pass SND_NORM (he's always on screen). */
    if (vol == SND_QUIET) return;
    if (event > 0 && event < sizeof(SFX2SND) && SFX2SND[event] != 0xFF)
        snd_play(SFX2SND[event]);
}

/* ---- rcfile save: never reached on GBC ---- */
void save_resource_file(char *path, int arg) { (void)path; (void)arg; }

/* ---- input stub (we drive move/shoot directly in the loop) ---- */
void manage_game_on_input(int actionid) { (void)actionid; }

static void start_level(void) {
    snd_stop();
    DISPLAY_OFF;
    level_init();
    render_gr_load();
    SHOW_BKG;
    WX_REG = 7; WY_REG = 128;      /* HUD window: bottom 2 rows */
    SHOW_WIN;
    DISPLAY_ON;
    snd_play(SND_ENTER);          /* Atari WFAC: Robbo appears */
}

/* The Atari scans its board every seven PAL frames. Our two-tick movement
   delays need half that period. Accumulate the PAL/GBC clock ratio in VBlank
   so rounding to display frames never speeds up or slows down the long-term
   cadence. A single pending flag coalesces overruns instead of queuing work. */
static volatile unsigned int game_tick_phase;
static volatile unsigned char game_tick_due;
static unsigned char game_anim_tick;

static void game_clock_vblank(void) {
    game_tick_phase += GR_PAL_PHASE_STEP;
    if (game_tick_phase < GR_PAL_PHASE_PERIOD) return;
    game_tick_phase -= GR_PAL_PHASE_PERIOD;
    game_tick_due = 1;
    /* CHNMON uses CNTR & 2: toggle after two Atari scans / four half-steps. */
    if (++game_anim_tick == 4) {
        game_anim_tick = 0;
        gr_anim_frame ^= 1;
    }
}

static void game_clock_reset(void) {
    __critical {
        game_tick_phase = 0;
        game_tick_due = 0;
    }
}

static unsigned char game_clock_take(void) {
    unsigned char due;
    __critical {
        due = game_tick_due;
        game_tick_due = 0;
    }
    return due;
}

void main(void) {
    unsigned char keys, prev = 0, pstart = 0, need_render = 1, last_screws = 0;
    int last_sel;

    if (_cpu == CGB_TYPE) cpu_fast();   /* CGB double-speed */
    DISPLAY_OFF;                        /* VRAM is locked while the LCD is on:
                                          load all tiles with the display off
                                          (start_level turns it back on) */

    /* pack/game state */
    selected_pack = 0;
#if ENDING_TEST
    level_packs[0].last_level = GR_NLEVELS + 1;   /* bonus test planet is the last */
#else
    level_packs[0].last_level = GR_NLEVELS;
#endif
    level_packs[0].level_selected = 1;
    level_packs[0].level_reached = 1;
    game_mode = GAME_ON;
    cycle_count = 0;
    game_mechanics.sensible_bears = 1;
    game_mechanics.sensible_questionmarks = 1;
    game_mechanics.sensible_solid_lasers = 1;
    rcfile.save_frequency = 0;

    snd_init();
    render_gr_init();
    hud_gr_init();
    add_VBL(render_gr_vblank);
    add_VBL(game_clock_vblank);
    gr_score = 0;

    render_gr_logo();       /* load the logo into font tiles 0..26 */
    title_gr_show();        /* title screen; returns on START */
    render_gr_init();       /* restore the font (title used the logo in 0..26) */

    start_level();
    last_sel = level_packs[0].level_selected;
    game_clock_reset();

    while (1) {
        wait_vbl_done();

        /* Last level finished (capsule): play the ending, then back to title. */
        if (game_mode == END_SCREEN) {
            render_gr_camera_pause();
            render_gr_ending();          /* HOME: load ending tiles into 0.. */
            ending_gr_show();            /* banked: animation + congratulations */
            game_mode = GAME_ON;
            render_gr_logo();
            title_gr_show();
            render_gr_init();
            level_packs[0].level_selected = 1;
            gr_score = 0;
            start_level();
            DISPLAY_OFF; hud_gr_init(); WX_REG = 7; WY_REG = 128; SHOW_WIN; DISPLAY_ON;
            last_sel = level_packs[0].level_selected;
            last_screws = (unsigned char)robbo.screws;
            need_render = 1; prev = pstart = 0;
            game_clock_reset();
            continue;
        }

        /* Start VRAM work in VBlank; GBDK's tile routines wait for accessible
           VRAM if a large redraw extends into the display period.  Scrolling
           is committed separately by the VBlank handler, never mid-scanline. */
        /* board cells + HUD only change on a game tick, so repaint them only
           then (not every frame) - this was the bulk of the render cost. */
        keys = joypad();
        /* Build the live overview before the normal renderer consumes the
           board's redraw flags. B changes only the view, not the tick clock. */
        overview_gr_update((keys & J_B) != 0);
        if (need_render) {
#if !(PROF & 2)
            show_game_area(); hud_gr_draw();
#endif
            need_render = 0;
        }
        /* (Robbo's death cell now explodes via the normal show_game_area path:
           cell_tiles renders his cell from board state, and kill_robbo's
           viewport_needs_redrawing + the BIG_BOOM animation set its redraw flag.
           The old per-frame render_gr_repaint() here was a full-screen VRAM
           rewrite that overran VBlank and slowed the whole death sequence.) */
#if !(PROF & 16)
        render_gr_camera();    /* stream ahead, then publish the camera target */
#endif
#if !(PROF & 4)
        render_gr_anim();      /* cycle animated object tiles */
#endif
        render_gr_robbo();     /* overlay robbo LAST so row streaming can't erase him */
        /* --- end VBlank-sensitive work --- */
        /* START -> pause menu, checked EVERY frame (its own edge tracking) so the
           press isn't swallowed by the tick gate's per-frame prev update. */
        keys = joypad();
        if ((keys & J_START) && !(pstart & J_START)) {
            unsigned char r;
            render_gr_camera_pause();
            r = pause_gr();
            if (r == 1) {                                   /* restart current level */
                start_level();
            } else if (r == 2) {                            /* warp to a chosen level */
                unsigned char w = warp_gr();
                if (w != 0xFF) { level_packs[0].level_selected = w + 1; start_level(); }
            } else if (r == 3) {                            /* quit to title */
                render_gr_logo();
                title_gr_show();
                render_gr_init();
                level_packs[0].level_selected = 1;
                gr_score = 0;
                start_level();
            }
            /* the menu drew over the HUD window - rebuild it (and reveal the game) */
            DISPLAY_OFF;
            hud_gr_init();
            WX_REG = 7; WY_REG = 128;
            SHOW_WIN;
            DISPLAY_ON;
            render_gr_camera_resume();
            last_sel = level_packs[0].level_selected;
            last_screws = (unsigned char)robbo.screws;
            need_render = 1;
            game_clock_reset();
            prev = pstart = keys;
            continue;
        }
        pstart = keys;

        /* The clock continues during object/render work. Consume at most one
           scheduled half-step; menus and level loads discard any pending tick. */
        if (!game_clock_take()) {
            prev = keys;
            continue;
        }

        if (game_mode == GAME_ON) {
            /* input: A held + dir = shoot, else dir = move (edge-triggered for
               a fresh press, repeat handled by robbo.moved/shooted delays) */
            if (robbo.alive) {
                if (keys & J_A) {
                    if (keys & J_UP)         shoot_robbo(0, -1);
                    else if (keys & J_DOWN)  shoot_robbo(0, 1);
                    else if (keys & J_LEFT)  shoot_robbo(-1, 0);
                    else if (keys & J_RIGHT) shoot_robbo(1, 0);
                } else {
                    if (keys & J_UP)         move_robbo(0, -1);
                    else if (keys & J_DOWN)  move_robbo(0, 1);
                    else if (keys & J_LEFT)  move_robbo(-1, 0);
                    else if (keys & J_RIGHT) move_robbo(1, 0);
                }
            }

            /* Capsule advanced the level: board.c (banked move_robbo) only bumped
               level_selected - do the level_init() reload HERE, in HOME context.
               level_init() switches ROM banks to read the level data and must NOT
               run from the banked move_robbo (it unmaps that bank -> garbage/hang,
               e.g. completing level 12).  Skip update_game this frame; the freshly
               loaded level updates on the next iteration. */
            if (level_packs[0].level_selected != last_sel) {
                last_sel = level_packs[0].level_selected;
                level_init();
                DISPLAY_OFF; render_gr_load(); DISPLAY_ON;
                last_screws = (unsigned char)robbo.screws;
                need_render = 1;
                game_clock_reset();
            } else {
#if !(PROF & 1)
                update_game();     /* logic only: sets redraw flags, no VRAM writes */
#endif
                need_render = 1;   /* repaint dirty cells next VBlank */

                /* score: robbo.screws is screws-left; a drop within the level
                   means screws were collected (+10 each). */
                {
                    unsigned char sc = (unsigned char)robbo.screws;
                    if (sc < last_screws)
                        gr_score += (unsigned long)(last_screws - sc) * 10;
                    last_screws = sc;
                }

                /* death countdown -> reload current level */
                if (restart_timeout > 0) {
                    if (--restart_timeout == 0) {
                        start_level();
                        last_sel = level_packs[0].level_selected;
                        game_clock_reset();
                    }
                }
            }
        }

        cycle_count++;
        prev = keys;
        (void)prev;
    }
}
