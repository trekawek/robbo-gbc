/* GBC renderer for the gnu-robbo port.  Reads board[][]/robbo directly and
   reuses the Atari font asset (gfx_tiles) + robbo-gbc's 2x2-metatile scrolling
   viewport.  Maps gnu-robbo object type -> an Atari glyph via type2asc + LOOK. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "gen/gfx_tiles.h"

/* LOOK: ATASCII object byte -> Atari screen code (low7=glyph, bit7=inverse). */
extern const unsigned char LOOK[128];

/* Per-level Atari palettes (BGR555): [0..3]=normal (palette 0), [4..7]=inverse
   (palette 1, used for walls).  Indexed by level-1.  See gr_atari_pal.c. */
#include "levels_data.h"
extern const unsigned int gr_atari_pal[GR_NLEVELS][8];
BANKREF_EXTERN(gr_atari_pal)

#define VIEW_W 160
#define PLAYH  128                         /* visible playfield height (px) */
#define ROBBO_TL 0x5E
#define ROBBO_BL 0x7E
#define MAX_SCX_(w) (((w) * 16) - VIEW_W)
#define MAX_SCY_(h) (((h) * 16) - PLAYH)

static unsigned char slot_owner[16];
static int scx_abs, scy_abs;
static int max_scx, max_scy;
static void set_sound_viewport(void);

/* gnu-robbo type -> ATASCII object byte (rendered via LOOK).  WALL handled
   specially (glyph 0 = the per-level wall char).  ROBBO drawn as an overlay. */
static const unsigned char type2asc[71] = {
    [EMPTY_FIELD]=0x20, [ROBBO]=0x20, [WALL]=0xA0, [WALL_RED]=0xA0,
    [SCREW]=0x24, [BULLET]=0x21, [BOX]=0x23, [KEY]=0x3D, [BOMB]=0x40,
    [DOOR]=0x7C, [QUESTIONMARK]=0x3F, [BEAR]=0x41, [BIRD]=0x4D, [CAPSULE]=0x14,
    [LITTLE_BOOM]=0x61, [GROUND]=0x25, [WALL_GREEN]=0xA0, [BEAR_B]=0x45,
    [BUTTERFLY]=0x26, [LASER_L]=0x5D, [LASER_D]=0x5B, [SOLID_LASER_L]=0x5D,
    [SOLID_LASER_D]=0x5B, [TELEPORT]=0x30, [TELEPORTING]=0x69, [BIG_BOOM]=0x61,
    [GUN]=0x2C, [MAGNET]=0x28, [BLASTER]=0x1C, [BLACK_WALL]=0xA0, [PUSH_BOX]=0x23,
    [BARRIER]=0x0F, [FAT_WALL]=0xA0, [ROUND_WALL]=0xA0, [BOULDER_WALL]=0xA0,
    [SQUARE_WALL]=0xA0, [LATTICE_WALL]=0xA0, [RADIOACTIVE_FIELD]=0x25, [STOP]=0x18,
    [BOMB2]=0x40,
};

/* CGB palette categories (see render_gr_load) */
#define PAL_NEUTRAL 0   /* empty/ground/box/push_box/quest */
#define PAL_WALL    1
#define PAL_ITEM    2   /* screw/key/ammo/life */
#define PAL_MONST   3   /* bear/bird/butterfly */
#define PAL_HAZARD  4   /* gun/laser/blaster/bomb/magnet/barrier/radioactive/stop */
#define PAL_EXIT    5   /* capsule/teleport/door */
#define PAL_BOOM    4   /* explosions share the hazard palette (6 freed for HUD) */

/* directional aim glyphs: index by gnu-robbo direction (0=E 1=S 2=W 3=N) */
static const unsigned char GUN_DIR[4]   = { 0x2C, 0x2D, 0x2E, 0x2F }; /* , - . /  (R D L U) */
static const unsigned char BLAST_DIR[4] = { 0x1F, 0x1D, 0x1E, 0x1C }; /* > v < ^ */

/* ATASCII glyph byte for a board cell (before LOOK), honouring direction/state */
static unsigned char cell_glyph(unsigned char t, unsigned char st, unsigned char dir) {
    switch (t) {
    case LITTLE_BOOM: case BIG_BOOM:  return (unsigned char)(0x61 + (st > 5 ? 5 : st));
    case TELEPORTING:                 return (unsigned char)(0x69 + (st > 4 ? 4 : st));
    /* Exit capsule: static (0x14) while closed; once the exit opens the board
       toggles its state every DELAY_CAPSULE, so it blinks between 0x14 and 0x16
       - the same glyph in the inverse palette (LOOK 0x14->0x1A, 0x16->0x9A),
       the authentic Atari open-exit flash. */
    case CAPSULE:                     return (unsigned char)((st & 1) ? 0x16 : 0x14);
    case GUN:                         return GUN_DIR[dir & 3];
    case BLASTER:                     return BLAST_DIR[dir & 3];
    /* magnet glyph: it scans in its 'direction' and pulls Robbo the opposite way
       (toward itself).  dir 0=East -> opening faces East -> '(' 0x28; dir 2=West
       -> ')' 0x29.  (Was reversed.) */
    case MAGNET:                      return (dir == 2) ? 0x29 : 0x28;
    default:                          return type2asc[t];
    }
}

/* palette category for an object type */
static unsigned char cell_pal(unsigned char t) {
    switch (t) {
    case WALL: case WALL_RED: case WALL_GREEN: case BLACK_WALL: case FAT_WALL:
    case ROUND_WALL: case BOULDER_WALL: case SQUARE_WALL: case LATTICE_WALL:
        return PAL_WALL;
    case SCREW: case KEY: case BULLET:               return PAL_ITEM;
    case BEAR: case BEAR_B: case BIRD: case BUTTERFLY: return PAL_MONST;
    case GUN: case LASER_L: case LASER_D: case SOLID_LASER_L: case SOLID_LASER_D:
    case BLASTER: case BOMB: case BOMB2: case MAGNET: case BARRIER:
    case RADIOACTIVE_FIELD: case STOP:               return PAL_HAZARD;
    case CAPSULE: case TELEPORT: case TELEPORTING: case DOOR: return PAL_EXIT;
    case LITTLE_BOOM: case BIG_BOOM:                 return PAL_BOOM;
    default:                                         return PAL_NEUTRAL;
    }
}

static void cell_tiles(unsigned char cx, unsigned char cy, unsigned char *tt, unsigned char *aa) {
    unsigned char t = board[cx][cy].type;
    unsigned char base, pal;
    if (cx == (unsigned char)robbo.x && cy == (unsigned char)robbo.y
        && t == EMPTY_FIELD && restart_timeout == 0) {
        /* Robbo stands on his (empty) cell - draw his sprite via the normal cell
           pipeline (no separate overlay).  render_gr_robbo() keeps the ROBBO_TL/BL
           tile pixels current; here we just place them.  Gated OUT while dying
           (restart_timeout > 0): kill_robbo makes his cell BIG_BOOM, but the
           explosion may later clear it back to EMPTY before the level reloads -
           without this check he'd be redrawn over the explosion.  Keying on the
           board type + restart_timeout (not robbo.alive, which reads stale here)
           means he explodes with the rest of the level. */
        tt[0]=ROBBO_TL; tt[1]=ROBBO_TL|1; tt[2]=ROBBO_BL; tt[3]=ROBBO_BL|1;
        aa[0]=aa[1]=aa[2]=aa[3]=0;   /* robbo uses the level's normal palette */
        return;
    }
    if (t == WALL) {
        base = 0;                                    /* wall glyph 0 (wall_chars) */
        pal = 1;                                     /* inverse palette = blue (Atari) */
    } else {
        /* Atari colour model: a cell uses the level's normal (0) or inverse (1)
           palette purely by the glyph's inverse bit - no semantic per-type tint. */
        unsigned char sc = LOOK[cell_glyph(t, board[cx][cy].state, board[cx][cy].direction)];
        base = sc & 0x7F;
        pal = (sc & 0x80) ? 1 : 0;
    }
    tt[0]=base; tt[1]=base|1; tt[2]=base|0x20; tt[3]=base|0x21;
    aa[0]=aa[1]=aa[2]=aa[3]=pal;
}

static void write_row(unsigned char r) {
    unsigned char tbuf[64], abuf[64], cx, t[4], a[4];
    unsigned char my = (unsigned char)((r & 15) * 2);
    for (cx = 0; cx < (unsigned char)level.w; cx++) {
        cell_tiles(cx, r, t, a);
        tbuf[cx*2]=t[0]; tbuf[cx*2+1]=t[1];
        tbuf[32+cx*2]=t[2]; tbuf[32+cx*2+1]=t[3];
        abuf[cx*2]=a[0]; abuf[cx*2+1]=a[1];
        abuf[32+cx*2]=a[2]; abuf[32+cx*2+1]=a[3];
    }
    set_bkg_tiles(0, my, 32, 2, tbuf);
    set_bkg_attributes(0, my, 32, 2, abuf);
    slot_owner[r & 15] = r;
}

static void ensure_visible(void) {
    int top = scy_abs >> 4, r;
    for (r = top - 1; r <= top + 9; r++) {
        if (r < 0 || r >= level.h) continue;
        if (slot_owner[r & 15] != (unsigned char)r) write_row((unsigned char)r);
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Keep Robbo's facet pixels current in the ROBBO_TL/BL tiles.  cell_tiles()
   PLACES those tiles at his cell (so every paint path renders him the same way -
   no overlay, no flicker, and on death his cell's BIG_BOOM renders instead).
   This only re-uploads the pixels when his facing/walk-frame changes.  robbo_chars
   holds 8 facets = 4 directions x 2 walk frames (dir*2+frame); the frame flips
   each time he steps to a new cell. */
void render_gr_robbo(void) {
    static const unsigned char dmap[4] = { 3, 1, 2, 0 }; /* gnu dir/2 -> facet */
    static unsigned char last_facet = 0xFF;
    unsigned char facet;
    /* Walk frame is derived DIRECTLY from game state, not from diffing his
       position across frames (which raced the tick/camera cadence: the diff
       was detected ~3 frames late and double-toggled per step, leaving him
       twitching after he stopped).  robbo.moved>0 == he's actively stepping
       (the game's per-step delay countdown); when it hits 0 he's standing, so
       show the neutral frame 0.  Matches the Atari original's `moving?walk:0`. */
    facet = (unsigned char)(dmap[(robbo.direction >> 1) & 3] * 2 + (robbo.moved > 0 ? 1 : 0));
    if (facet != last_facet) {
        last_facet = facet;
        SWITCH_ROM(GFX_BANK);
        set_bkg_data(ROBBO_TL, 2, robbo_chars + (unsigned int)facet * 64);
        set_bkg_data(ROBBO_BL, 2, robbo_chars + (unsigned int)facet * 64 + 32);
    }
}

static unsigned int rgb24_to_bgr555(unsigned long c) {
    unsigned int r = (unsigned int)((c >> 19) & 0x1F);
    unsigned int g = (unsigned int)((c >> 11) & 0x1F);
    unsigned int b = (unsigned int)((c >> 3) & 0x1F);
    return (unsigned int)(b << 10) | (g << 5) | r;
}

void render_gr_init(void) {
    SWITCH_ROM(GFX_BANK);
    set_bkg_data(0, FONT_NTILES, font_tiles);
    set_bkg_data(128, 96, ifnt_tiles);          /* I.FNT digits/icons for the HUD */
    set_bkg_data(ROBBO_TL, 2, &robbo_chars[2*64]);
    set_bkg_data(ROBBO_BL, 2, &robbo_chars[2*64 + 32]);
}

/* Load the title logo into tiles 0..LOGO_NTILES-1 (over the playfield font).
   Done here in HOME because reading the banked logo needs a SWITCH_ROM, which
   the banked title module can't do to itself.  Call render_gr_init() afterwards
   to restore the font. */
/* WRAM copy of the banked rainbow palette table so the banked title module can
   read it without a SWITCH_ROM-to-self (the banked-fn gotcha). */
unsigned int gr_logo_rainbow[LOGO_RAINBOW_N][4];

void render_gr_logo(void) {
    unsigned char k;
    SWITCH_ROM(GFX_BANK);
    set_bkg_data(0, LOGO_NTILES, logo_tiles);
    for (k = 0; k < LOGO_RAINBOW_N; k++) {
        gr_logo_rainbow[k][0] = logo_rainbow[k][0];
        gr_logo_rainbow[k][1] = logo_rainbow[k][1];
        gr_logo_rainbow[k][2] = logo_rainbow[k][2];
        gr_logo_rainbow[k][3] = logo_rainbow[k][3];
    }
}

void render_gr_load(void) {
    unsigned int pal[8];
    unsigned char i, g;
    /* Atari per-level palette: palette 0 (normal) + palette 1 (inverse=walls).
       Indexed by level number-1; this is where the green floor + blue walls come
       from (the authentic Atari colours, replacing the old semantic scheme). */
    unsigned char idx = level_packs[selected_pack].level_selected;
    if (idx < 1) idx = 1;
    if (idx > GR_NLEVELS) idx = GR_NLEVELS;
    idx--;
    SWITCH_ROM(BANK(gr_atari_pal));        /* table lives in a switchable bank */
    for (i = 0; i < 8; i++) pal[i] = gr_atari_pal[idx][i];
    set_bkg_palette(0, 2, (const palette_color_t *)pal);   /* palettes 0 + 1 */
    /* HUD bar keeps its own palette 6 (black bg, white digits). */
    {
        static const unsigned int hudpal[4] = {0x0000,0x7FFF,0x167A,0x7FFF};
        set_bkg_palette(6, 1, (const palette_color_t *)hudpal);
    }
    SWITCH_ROM(GFX_BANK);
    g = (unsigned char)(selected_pack & 0); /* fixed wall group 0 for now */
    if (g >= WALL_NGROUPS) g = 0;
    set_bkg_data(0,    2, &wall_chars[g*64]);
    set_bkg_data(0x20, 2, &wall_chars[g*64 + 32]);
    for (i = 0; i < 16; i++) slot_owner[i] = 0xFF;
    max_scx = MAX_SCX_(level.w); if (max_scx < 0) max_scx = 0;
    max_scy = MAX_SCY_(level.h); if (max_scy < 0) max_scy = 0;
    scx_abs = clampi((int)robbo.x * 16 + 8 - 80, 0, max_scx);
    scy_abs = clampi((int)robbo.y * 16 + 8 - 64, 0, max_scy);
    set_sound_viewport();
    ensure_visible();
    SCX_REG = (unsigned char)scx_abs;
    SCY_REG = (unsigned char)(scy_abs & 0xFF);
    /* we just drew every cell (display is off here); clear the redraw flags so
       the first in-game show_game_area doesn't repaint all 496 cells at once
       (which would overrun a single VBlank). */
    {
        unsigned int n;
        struct object *p = &board[0][0];
        for (n = 0; n < (unsigned int)MAX_W * MAX_H; n++, p++) p->redraw = 0;
    }
}

/* Camera ease speed (px per loop iteration).  render_gr_camera runs once per
   loop iteration; the logic/cycle runs every GR_TICK_GATE iterations and robbo
   advances 16px every DELAY_ROBBO(=2) cycles.  For the camera to keep up during
   continuous movement we need GR_TICK_GATE*SPD >= 16/DELAY_ROBBO = 8.  Deriving
   SPD from the gate keeps the camera tracking at any gate (smaller gate -> bigger
   but fewer steps).  gate 3->3px, 2->4px, 1->8px. */
#define SPD ((8 + GR_TICK_GATE - 1) / GR_TICK_GATE)
static int ease(int cur, int tgt) {
    int d = tgt - cur;
    if (d > SPD) return cur + SPD;
    if (d < -SPD) return cur - SPD;
    return tgt;
}

/* Mirror the GBC camera window (in cells) into board.c's `viewport`, so
   in_viewport() - which the engine uses to gate world-event sounds (gun/bird/
   bomb/kill) to NORM vs QUIET - reflects what is ACTUALLY on screen.  The
   whole-board viewport set in level_init() made every off-screen shot audible
   (the "constant cracking"); play_sound() now drops the QUIET (off-screen) ones. */
static void set_sound_viewport(void) {
    viewport.x = scx_abs >> 4;
    viewport.y = scy_abs >> 4;
    viewport.w = ((scx_abs + VIEW_W - 1) >> 4) - viewport.x;
    viewport.h = ((scy_abs + PLAYH - 1) >> 4) - viewport.y;
}

void render_gr_camera(void) {
    int tx = clampi((int)robbo.x * 16 + 8 - 80, 0, max_scx);
    int ty = clampi((int)robbo.y * 16 + 8 - 64, 0, max_scy);
    scx_abs = ease(scx_abs, tx);
    scy_abs = ease(scy_abs, ty);
    set_sound_viewport();
    ensure_visible();
    SCX_REG = (unsigned char)scx_abs;
    SCY_REG = (unsigned char)(scy_abs & 0xFF);
}

/* Ambient twinkle: cycle the 2-frame animated object tiles (screws/teleports/
   etc.) in their fixed tile slots, spread across frames to avoid VBlank overrun.
   Call once per frame.  Mirrors the original FNT animation. */
void render_gr_anim(void) {
    static unsigned char ctr, frame, batch;
    unsigned char i, start, end;
    if (++ctr >= 16) { ctr = 0; frame ^= 1; batch = 0; }  /* flip frame every 16 */
    start = (unsigned char)(batch * 6);
    if (start >= ANIM_NCHARS) return;
    end = (unsigned char)(start + 6);
    if (end > ANIM_NCHARS) end = ANIM_NCHARS;
    SWITCH_ROM(GFX_BANK);
    /* upload at most 6 tiles per frame so the whole 32-tile swap is spread
       across several frames and never overruns a single VBlank */
    for (i = start; i < end; i++)
        set_bkg_data(anim_slots[i], 1, (frame ? anim_b : anim_a) + (unsigned int)i * 16);
    batch++;
}

/* show_game_area: repaint board cells whose redraw flag is set, then robbo.
   Scans the board linearly via a pointer (no per-cell x*MAX_H+y multiply on
   the GB's software multiplier); x/y are only derived for the few dirty cells. */
int show_game_area(void) {
    unsigned int i;
    struct object *p = &board[0][0];
    unsigned char t[4], a[4], x, y;
    for (i = 0; i < (unsigned int)MAX_W * MAX_H; i++, p++) {
        if (!p->redraw) continue;
        p->redraw = 0;
        y = (unsigned char)(i % MAX_H);
        if (slot_owner[y & 15] != y) continue;
        x = (unsigned char)(i / MAX_H);
        cell_tiles(x, y, t, a);     /* cell_tiles() renders robbo on his own cell too */
        set_bkg_tiles(x*2, (y & 15)*2, 2, 2, t);
        set_bkg_attributes(x*2, (y & 15)*2, 2, 2, a);
    }
    return 0;
}

int show_game_area_fade(int subfunction, int type) { (void)subfunction; (void)type; return 0; }
