#include <gb/gb.h>
#include <gb/cgb.h>
#include "render.h"
#include "cave.h"
#include "game.h"
#include "object_tables.h"
#include "gen/gfx_tiles.h"

/* robbo facet chars are blank in F.FNT; overlaid from S.FNT into these slots */
#define ROBBO_TL 0x5E
#define ROBBO_BL 0x7E

#define MAX_SCX  ((LVL_W * 16) - 160)        /* 96  */
#define MAX_SCY  ((LVL_H * 16) - PLAYFIELD_PX) /* 368 */

static unsigned char slot_owner[16];   /* which cave row is in map slot (row&15) */
static int scx_abs, scy_abs;           /* current eased scroll position, px */

/* ---- the 4 tiles + palettes for one cave cell (2x2, row-major TL,TR,BL,BR) ---- */
static void cell_tiles(unsigned char cx, unsigned char cy,
                       unsigned char *t, unsigned char *a) {
    unsigned char b = CAVE_AT(cx, cy);
    unsigned char sc, base, pal;
    if (b >= 0x80) b = 0;              /* walls / static -> glyph 0 */
    sc = LOOK[b];
    base = sc & 0x7F;
    pal = (sc & 0x80) ? 1 : 0;         /* inverse colour -> palette 1 */
    t[0] = base; t[1] = base | 1; t[2] = base | 0x20; t[3] = base | 0x21;
    a[0] = a[1] = a[2] = a[3] = pal;
}

static void write_row(unsigned char r) {
    unsigned char tbuf[64], abuf[64], cx, t[4], a[4];
    unsigned char my = (unsigned char)((r & 15) * 2);
    for (cx = 0; cx < LVL_W; cx++) {
        cell_tiles(cx, r, t, a);
        tbuf[cx*2] = t[0]; tbuf[cx*2+1] = t[1];
        tbuf[32+cx*2] = t[2]; tbuf[32+cx*2+1] = t[3];
        abuf[cx*2] = a[0]; abuf[cx*2+1] = a[1];
        abuf[32+cx*2] = a[2]; abuf[32+cx*2+1] = a[3];
    }
    set_bkg_tiles(0, my, 32, 2, tbuf);
    set_bkg_attributes(0, my, 32, 2, abuf);
    slot_owner[r & 15] = r;
}

static void ensure_visible(void) {
    int top = scy_abs >> 4;            /* top cave row in view */
    int r;
    for (r = top - 1; r <= top + 9; r++) {   /* 9 visible rows + margin */
        if (r < 0 || r >= LVL_H) continue;
        if (slot_owner[r & 15] != (unsigned char)r) write_row((unsigned char)r);
    }
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* constant-speed follow: smoother than an exponential ease, keeps pace with
   robbo's ~3.2px/frame grid steps */
#define SCROLL_SPEED 3
static int ease(int cur, int target) {
    int d = target - cur;
    if (d > SCROLL_SPEED)  return cur + SCROLL_SPEED;
    if (d < -SCROLL_SPEED) return cur - SCROLL_SPEED;
    return target;
}

void render_init(void) {
    SWITCH_ROM(GFX_BANK);                          /* tiles live in a banked module */
    set_bkg_data(0, FONT_NTILES, font_tiles);
    set_bkg_data(ROBBO_TL, 2, &robbo_chars[2 * 64]);      /* default: facing down, frame 0 */
    set_bkg_data(ROBBO_BL, 2, &robbo_chars[2 * 64 + 32]);
}

void render_load_level(const Level *l, unsigned char idx) {
    unsigned char i, g;
    unsigned int pal[8];
    /* copy the palette out of the level's ROM bank before we switch banks */
    for (i = 0; i < 8; i++) pal[i] = l->pal[i];
    set_bkg_palette(0, 2, (const palette_color_t *)pal);

    /* per-level wall chars from M.FNT group -> glyph 0,1,0x20,0x21 */
    SWITCH_ROM(GFX_BANK);
    g = (unsigned char)((idx >> 2) & 0x0F);
    if (g >= WALL_NGROUPS) g = 0;
    set_bkg_data(0,    2, &wall_chars[g*64]);
    set_bkg_data(0x20, 2, &wall_chars[g*64 + 32]);

    for (i = 0; i < 16; i++) slot_owner[i] = 0xFF;
    scx_abs = clampi((int)l->robbo_x * 16 + 8 - 80, 0, MAX_SCX);
    scy_abs = clampi((int)l->robbo_y * 16 + 8 - 64, 0, MAX_SCY);
    ensure_visible();
    SCX_REG = (unsigned char)scx_abs;
    SCY_REG = (unsigned char)(scy_abs & 0xFF);
}

void render_cell(unsigned char cx, unsigned char cy) {
    unsigned char t[4], a[4];
    if (slot_owner[cy & 15] != cy) return;     /* row not currently mapped */
    cell_tiles(cx, cy, t, a);
    set_bkg_tiles(cx*2, (cy & 15)*2, 2, 2, t);
    set_bkg_attributes(cx*2, (cy & 15)*2, 2, 2, a);
}

void render_repaint(void) {
    unsigned char i;
    for (i = 0; i < 16; i++) slot_owner[i] = 0xFF;
    ensure_visible();
}

void render_anim(unsigned char facing, unsigned char moving) {
    static unsigned char frame, ctr, batch, last_facet = 0xFF, walk;
    static unsigned char lx = 0xFF, ly = 0xFF;
    unsigned char i, want;
    const unsigned char *f;
    VBK_REG = 0;                  /* tile data lives in VRAM bank 0 */
    SWITCH_ROM(GFX_BANK);         /* robbo/anim tiles live in a banked module */

    /* Robbo holds the facet for the direction he last faced - no idle animation -
       but his stride animates WHILE he walks: the original FACET routine flips
       between two frames each step (RUCH set -> INC the frame).  We flip the walk
       frame each time he changes cell.  robbo_chars holds 8 facets, indexed
       (dir*2 + frame), dir in gs.facing order (0=up 1=down 2=left 3=right),
       each 64 bytes = TL,TR (top) + BL,BR (bottom). */
    if (gs.robbo_x != lx || gs.robbo_y != ly) {     /* stepped to a new cell */
        lx = gs.robbo_x; ly = gs.robbo_y; walk ^= 1;
    }
    want = (unsigned char)(facing * 2 + ((moving && walk) ? 1 : 0));
    if (want != last_facet) {
        last_facet = want;
        f = robbo_chars + (unsigned int)want * 64;
        set_bkg_data(ROBBO_TL, 2, f);          /* top    0x5E,0x5F */
        set_bkg_data(ROBBO_BL, 2, f + 32);     /* bottom 0x7E,0x7F */
    }

    /* Toggle the animation frame every 16 frames, but SPREAD the 24-tile upload
       across several frames - doing all of them in one VBlank overruns it and
       the tail tiles (teleports) get dropped, which also causes scroll hitches.
       (16, not 8: the original twinkles at ~half this; 8 ran visibly 2x too fast.) */
    if (++ctr >= 16) { ctr = 0; frame ^= 1; batch = 0; }
    if (batch * 6 < ANIM_NCHARS) {
        unsigned char start = batch * 6, end = start + 6;
        if (end > ANIM_NCHARS) end = ANIM_NCHARS;
        for (i = start; i < end; i++)
            set_bkg_data(anim_slots[i], 1, (frame ? anim_b : anim_a) + i * 16);
        batch++;
    }
}

void render_update(unsigned char robbo_x, unsigned char robbo_y) {
    int tx = clampi((int)robbo_x * 16 + 8 - 80, 0, MAX_SCX);
    int ty = clampi((int)robbo_y * 16 + 8 - 64, 0, MAX_SCY);
    scx_abs = ease(scx_abs, tx);
    scy_abs = ease(scy_abs, ty);
    ensure_visible();
    SCX_REG = (unsigned char)scx_abs;
    SCY_REG = (unsigned char)(scy_abs & 0xFF);
}

/* Snap (no ease) the camera to centre robbo - used by the teleport effect so the
   materialise blink at a far destination is actually on-screen. */
void render_snap(unsigned char robbo_x, unsigned char robbo_y) {
    scx_abs = clampi((int)robbo_x * 16 + 8 - 80, 0, MAX_SCX);
    scy_abs = clampi((int)robbo_y * 16 + 8 - 64, 0, MAX_SCY);
    ensure_visible();
    SCX_REG = (unsigned char)scx_abs;
    SCY_REG = (unsigned char)(scy_abs & 0xFF);
}
