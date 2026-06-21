#include "objects.h"
#include "game.h"
#include "cave.h"
#include "object_tables.h"
#include "player.h"
#include "sound.h"

/* ---- object byte classes (ATASCII codes, matching the original) ----
   bullets PO1 : 0x4F O(left) 0x50 P(right) 0x51 Q(up) 0x52 R(down)
   monsters STW: 0x41..0x4C 'A'..'L' (wall-followers + bouncers)
   bats   TOP  : 0x4D 'M'(left) 0x4E 'N'(right)  (move + shoot down)
   chaser LEDZ : 0x26 '&'
   magnet MGN  : 0x28 '(' (pull left), 0x29 ')' (pull right)
   blasters DZ1: arrows 0x1C^ 0x1D v 0x1E< 0x1F>   DZ2: 0x22" 0x3C< 0x3E> 0x5E^
            DZ3: 0x01 0x04 0x17 0x18 (pipe glyphs)
   surprise    : 0x3F '?'   bomb 0x40 '@'  (exploding 0x60 '`') */
#define IS_BULLET(b)  ((b) >= 0x4F && (b) <= 0x52)
#define IS_STW(b)     ((b) >= 0x41 && (b) <= 0x4C)
#define IS_BAT(b)     ((b) == 0x4D || (b) == 0x4E)

/* dirs: 0=up 1=down 2=left 3=right (match DX/DY in game.c) */
static const unsigned char BULLET_DIR[4] = { 0x51, 0x52, 0x4F, 0x50 };

/* done[] marks cells already processed this tick.  Instead of zeroing all 496
   bytes every tick, we stamp the current generation and bump it - only clearing
   on the rare wrap. */
static unsigned char done[LVL_W * LVL_H];
static unsigned char done_gen;
static unsigned char tickctr;              /* CNTR: tick counter for throttled objects */

/* ---- tiny PRNG (Atari used POKEY RND) ---- */
static unsigned int rng = 0xACE1;
static unsigned char rnd(void) {
    rng ^= rng << 7; rng ^= rng >> 9; rng ^= rng << 3;
    return (unsigned char)rng;
}

static void mark(unsigned char x, unsigned char y) { done[(unsigned int)y*LVL_W + x] = done_gen; }
static unsigned char is_done(unsigned char x, unsigned char y) { return done[(unsigned int)y*LVL_W + x] == done_gen; }
void obj_mark(unsigned char x, unsigned char y) { mark(x, y); }
static unsigned char at_robbo(signed char x, signed char y) {
    return (unsigned char)x == gs.robbo_x && (unsigned char)y == gs.robbo_y;
}

/* ---- explosion: a destroyed cell becomes 'a', then advances a..g -> space ---- */
#define EXPL_START 0x61                  /* 'a' */
#define IS_ANIM(b) ((b) >= 0x61 && (b) <= 0x7A)
static void explode_cell(unsigned char x, unsigned char y) {
    cave_set(x, y, EXPL_START);
    mark_dirty(x, y);
    mark(x, y);                          /* don't advance the new frame this same tick */
}
static void advance_anim(unsigned char x, unsigned char y, unsigned char b) {
    unsigned char n = NEXT[b - 0x61];    /* a->b ... g->space */
    cave_set(x, y, n);
    mark_dirty(x, y);
    if (IS_ANIM(n)) mark(x, y);
}

/* ---- bomb ---- */
void bomb_explode(unsigned char cx, unsigned char cy) {
    signed char x, y;
    snd_play(SND_EXPLODE);
    explode_cell(cx, cy);
    for (y = (signed char)cy - 1; y <= (signed char)cy + 1; y++) {
        for (x = (signed char)cx - 1; x <= (signed char)cx + 1; x++) {
            unsigned char b;
            if (x < 0 || x >= LVL_W || y < 0 || y >= LVL_H) continue;
            if (x == (signed char)cx && y == (signed char)cy) continue;
            b = cave_get(x, y);
            if (b == OB_WALL) continue;                 /* solid walls survive */
            if (at_robbo(x, y)) gs.dead = 1;
            if (IS_STW(b) || IS_BAT(b) || b == 0x26) gs.score += 10;
            if (b == OB_BOMB) {                              /* chain on its NEXT tick */
                cave_set((unsigned char)x, (unsigned char)y, 0x60);
                mark_dirty((unsigned char)x, (unsigned char)y);
                mark((unsigned char)x, (unsigned char)y);
                continue;
            }
            explode_cell((unsigned char)x, (unsigned char)y);
        }
    }
}

/* ---- ? surprise (NIES): a shot capsule yields a random item from the original
   32-entry NSPS table; the two zero entries are a screen-clearing jackpot ---- */
static const unsigned char NSPS[32] = {
    0x00,0x00,0x3F,0x3F,0x21,0x21,0x21,0x21,   /* ?? !!!! */
    0x3D,0x3D,0x3D,0x24,0x24,0x24,0x24,0x24,   /* === $$$$$ */
    0x26,0x26,0x40,0x2B,0x2B,0x2B,0x2B,0x2B,   /* && @ +++++ */
    0x63,0x63,0x15,0x2C,0x2E,0x2D,0x2F,0x23    /* cc . ,.-/ # */
};
static void surprise_at(unsigned char x, unsigned char y) {
    unsigned char v = NSPS[rnd() & 31];
    if (v == 0) {                              /* jackpot: bonus life + clear monsters */
        unsigned int j;
        for (j = 0; j < LVL_W * LVL_H; j++) {
            unsigned char b = cave[j];
            if (IS_STW(b) || IS_BAT(b) || b == 0x26) {
                cave[j] = EXPL_START;
                mark_dirty((unsigned char)(j % LVL_W), (unsigned char)(j / LVL_W));
            }
        }
        cave_set(x, y, OB_LIFE);
        gs.score += 500;
        snd_play(SND_CAPSULE);
    } else {
        cave_set(x, y, v);
        snd_play(SND_SCREW);
    }
    mark_dirty(x, y);
    mark(x, y);
}

/* a bullet (or shot) lands on cell (x,y): resolve what it hits, exactly as the
   original PPP1 does — destructibility is WHAT[t] bit0 (the same table the game
   uses), with bombs and '?' capsules special-cased. */
static void bullet_hit(unsigned char x, unsigned char y, unsigned char t) {
    if (at_robbo(x, y)) { gs.dead = 1; return; }
    if (t >= 0x80) return;                           /* wall (high-bit glyph): indestructible -
                                                        WHAT is a 128-entry table, and t&0x7F
                                                        would alias a wall onto a destructible
                                                        low glyph (0xA0 -> 0x20), letting lasers
                                                        eat through walls */
    if (!(WHAT[t & 0x7F] & 1)) return;               /* not destructible: bullet absorbed
                                                        (no ricochet SFX - lasers hit walls
                                                        every tick and would spam the one
                                                        sound channel, cutting other effects) */
    if (t == OB_BOMB)  { bomb_explode(x, y); return; }   /* '@' detonate */
    if (t == OB_QUEST) { surprise_at(x, y); return; }    /* '?' reveal loot */
    explode_cell(x, y);                              /* monster/ground/'+'/'!'/field */
    if (IS_STW(t) || IS_BAT(t) || t == 0x26) gs.score += 10;
    snd_play(SND_DESTROY);
}
void obj_bullet_hit(unsigned char x, unsigned char y, unsigned char t) { bullet_hit(x, y, t); }

static void move_bullet(unsigned char x, unsigned char y, unsigned char b) {
    unsigned char dir = (b == 0x51) ? 0 : (b == 0x52) ? 1 : (b == 0x4F) ? 2 : 3;
    signed char nx = (signed char)x + DX[dir], ny = (signed char)y + DY[dir];
    unsigned char t;
    cave_set(x, y, OB_SPACE); mark_dirty(x, y);
    if (nx < 0 || nx >= LVL_W || ny < 0 || ny >= LVL_H) return;
    t = cave_get(nx, ny);
    if (t == OB_SPACE) {                               /* travel on */
        cave_set((unsigned char)nx, (unsigned char)ny, b);
        mark_dirty((unsigned char)nx, (unsigned char)ny);
        mark((unsigned char)nx, (unsigned char)ny);
        return;
    }
    bullet_hit((unsigned char)nx, (unsigned char)ny, t);
}

/* ---- DZ1 blasters (arrows): single audible bullet ---- */
static signed char blaster_dir(unsigned char b) {
    switch (b) {
        case 0x1C: return 0;   /* ^ up   */
        case 0x1D: return 1;   /* v down */
        case 0x1E: return 2;   /* < left */
        case 0x1F: return 3;   /* > right*/
        default: return -1;
    }
}

static void do_fire(unsigned char x, unsigned char y, unsigned char dir) {
    signed char nx = (signed char)x + DX[dir], ny = (signed char)y + DY[dir];
    unsigned char t;
    if (nx < 0 || nx >= LVL_W || ny < 0 || ny >= LVL_H) return;
    t = cave_get(nx, ny);
    snd_play(SND_SHOOT);
    if (t == OB_SPACE) {
        cave_set((unsigned char)nx, (unsigned char)ny, BULLET_DIR[dir]);
        mark_dirty((unsigned char)nx, (unsigned char)ny);
        mark((unsigned char)nx, (unsigned char)ny);
    } else {
        bullet_hit((unsigned char)nx, (unsigned char)ny, t);
    }
}
static void fire_blaster(unsigned char x, unsigned char y, unsigned char dir) {
    if (rnd() < 18) do_fire(x, y, dir);                /* ~7% chance/tick */
}

/* ---- DZ2 laser: each cannon (" < > ^) fires an independent beam that, like
   the original PO2 projectile, extends one cell per tick toward the first
   obstacle (laying a ']'/'[' trail), HOLDS fully extended and solid for a beat
   (so it reads as a static beam, not a constant in/out pulse), then retracts
   one cell per tick.  Only the advancing tip is lethal. ---- */
#define LASER_V 0x5B    /* vertical beam segment   ('[') */
#define LASER_H 0x5D    /* horizontal beam segment (']') */
static unsigned char is_laser(unsigned char b) {
    return b == 0x22 || b == 0x3C || b == 0x3E || b == 0x5E;
}
static unsigned char laser_dir(unsigned char b) {
    return b == 0x5E ? 0 : b == 0x22 ? 1 : b == 0x3C ? 2 : 3;
}

#define MAX_LASER 6
#define LASER_HOLD 10   /* ticks the fully-extended beam stays solid before retracting */
static struct { unsigned char x, y, dir, len, hold, retract, active; } lbeam[MAX_LASER];

/* A laser cannon at (x,y) fires ~7%/tick, but only if it has no live beam yet. */
static void laser_seed(unsigned char x, unsigned char y, unsigned char dir) {
    unsigned char i;
    for (i = 0; i < MAX_LASER; i++)
        if (lbeam[i].active && lbeam[i].x == x && lbeam[i].y == y) return;
    if (rnd() >= 18) return;
    for (i = 0; i < MAX_LASER; i++)
        if (!lbeam[i].active) {
            lbeam[i].x = x; lbeam[i].y = y; lbeam[i].dir = dir;
            lbeam[i].len = 0; lbeam[i].hold = 0; lbeam[i].retract = 0; lbeam[i].active = 1;
            return;
        }
}

/* Advance/retract every live beam INCREMENTALLY: extend draws only the new tip
   cell, retract clears only the vacated tip cell, and the held body is left
   untouched.  Redrawing the whole body each tick (clear+repaint) made every
   segment blink to 'space' for one frame per tick - this keeps the body static.
   Beam body glyphs are inert in the scan (active_tbl[ ']' / '[' ] == 0). */
static void laser_update(void) {
    unsigned char i;
    for (i = 0; i < MAX_LASER; i++) {
        unsigned char dir, seg, t;
        signed char nx, ny;
        if (!lbeam[i].active) continue;
        dir = lbeam[i].dir;
        seg = (dir < 2) ? LASER_V : LASER_H;
        if (lbeam[i].hold) {                      /* fully extended: solid, no redraw */
            if (--lbeam[i].hold == 0) lbeam[i].retract = 1;
        } else if (!lbeam[i].retract) {           /* extend: draw only the new tip cell */
            nx = (signed char)lbeam[i].x + (signed char)(DX[dir] * (lbeam[i].len + 1));
            ny = (signed char)lbeam[i].y + (signed char)(DY[dir] * (lbeam[i].len + 1));
            if (nx < 0 || nx >= LVL_W || ny < 0 || ny >= LVL_H) lbeam[i].hold = LASER_HOLD;
            else if (at_robbo(nx, ny)) { gs.dead = 1; lbeam[i].hold = LASER_HOLD; }
            else {
                t = cave_get((unsigned char)nx, (unsigned char)ny);
                if (t == OB_SPACE) {                          /* tip advances one cell */
                    lbeam[i].len++;
                    cave_set((unsigned char)nx, (unsigned char)ny, seg);
                    mark_dirty((unsigned char)nx, (unsigned char)ny);
                } else { bullet_hit((unsigned char)nx, (unsigned char)ny, t); lbeam[i].hold = LASER_HOLD; }
            }
        } else {                                  /* retract: clear only the current tip cell */
            if (lbeam[i].len == 0) { lbeam[i].active = 0; continue; }
            nx = (signed char)lbeam[i].x + (signed char)(DX[dir] * lbeam[i].len);
            ny = (signed char)lbeam[i].y + (signed char)(DY[dir] * lbeam[i].len);
            if (cave_get((unsigned char)nx, (unsigned char)ny) == seg) {   /* don't clobber others */
                cave_set((unsigned char)nx, (unsigned char)ny, OB_SPACE);
                mark_dirty((unsigned char)nx, (unsigned char)ny);
            }
            lbeam[i].len--;
        }
    }
}

/* ---- DZ3 blasters (pipe glyphs): silent projectile that leaves a burning
   trail of explosion cells and detonates bombs/'?' in its path ---- */
#define IS_DZ3_BULLET(b) ((b) >= 0x57 && (b) <= 0x5A)   /* W X Y Z */
static const unsigned char DZ3_BULLET[4] = { 0x59, 0x5A, 0x57, 0x58 }; /* up,down,left,right */
static unsigned char is_dz3(unsigned char b) {
    return b == 0x01 || b == 0x04 || b == 0x17 || b == 0x18;
}
static unsigned char dz3_dir(unsigned char b) {
    return b == 0x18 ? 0 : b == 0x17 ? 1 : b == 0x04 ? 2 : 3;
}
static void fire_dz3(unsigned char x, unsigned char y, unsigned char dir) {
    signed char nx = (signed char)x + DX[dir], ny = (signed char)y + DY[dir];
    unsigned char t;
    if (rnd() >= 18) return;                     /* ~7%/tick, silent (no sound) */
    if (nx < 0 || nx >= LVL_W || ny < 0 || ny >= LVL_H) return;
    t = cave_get((unsigned char)nx, (unsigned char)ny);
    if (t == OB_SPACE) {
        cave_set((unsigned char)nx, (unsigned char)ny, DZ3_BULLET[dir]);
        mark_dirty((unsigned char)nx, (unsigned char)ny);
        mark((unsigned char)nx, (unsigned char)ny);
    } else {
        bullet_hit((unsigned char)nx, (unsigned char)ny, t);
    }
}
static void move_dz3_bullet(unsigned char x, unsigned char y, unsigned char b) {
    unsigned char dir = (b == 0x59) ? 0 : (b == 0x5A) ? 1 : (b == 0x57) ? 2 : 3;
    signed char nx = (signed char)x + DX[dir], ny = (signed char)y + DY[dir];
    unsigned char t;
    cave_set(x, y, 0x62);                          /* leave a 'b' explosion trail */
    mark_dirty(x, y); mark(x, y);
    if (nx < 0 || nx >= LVL_W || ny < 0 || ny >= LVL_H) return;
    if (at_robbo(nx, ny)) { gs.dead = 1; return; }
    t = cave_get((unsigned char)nx, (unsigned char)ny);
    if (t == OB_SPACE) {
        cave_set((unsigned char)nx, (unsigned char)ny, b);
        mark_dirty((unsigned char)nx, (unsigned char)ny);
        mark((unsigned char)nx, (unsigned char)ny);
    } else {
        bullet_hit((unsigned char)nx, (unsigned char)ny, t);
    }
}

/* ---- STW monsters: wall-follower / bouncer state machine ---- */
/* per state (byte-0x41): {dir1, state1, dir2, state2_blocked}; dir2=0xFF if none */
static const unsigned char STW[12][4] = {
    /*A*/ {2,0x44, 0,0x43}, /*B*/ {3,0x43, 1,0x44},
    /*C*/ {0,0x41, 3,0x42}, /*D*/ {1,0x42, 2,0x41},
    /*E*/ {2,0x47, 1,0x48}, /*F*/ {3,0x48, 0,0x47},
    /*G*/ {0,0x46, 2,0x45}, /*H*/ {1,0x45, 3,0x46},
    /*I*/ {2,0x49, 0xFF,0x4A}, /*J*/ {3,0x4A, 0xFF,0x49},
    /*K*/ {0,0x4B, 0xFF,0x4C}, /*L*/ {1,0x4C, 0xFF,0x4B},
};

/* robbo orthogonally adjacent to (x,y)? -> kills robbo */
static unsigned char touches_robbo(unsigned char x, unsigned char y) {
    unsigned char d;
    for (d = 0; d < 4; d++)
        if (at_robbo((signed char)x + DX[d], (signed char)y + DY[d])) return 1;
    return 0;
}

static unsigned char cell_free(signed char x, signed char y) {
    if (x < 0 || x >= LVL_W || y < 0 || y >= LVL_H) return 0;
    return cave_get(x, y) == OB_SPACE;
}

static void move_to(unsigned char x, unsigned char y, unsigned char dir, unsigned char newb) {
    unsigned char nx = (unsigned char)((signed char)x + DX[dir]);
    unsigned char ny = (unsigned char)((signed char)y + DY[dir]);
    cave_set(x, y, OB_SPACE); mark_dirty(x, y);
    cave_set(nx, ny, newb);   mark_dirty(nx, ny);
    mark(nx, ny);
}

static void move_stw(unsigned char x, unsigned char y, unsigned char b) {
    const unsigned char *e = STW[b - 0x41];
    if (touches_robbo(x, y)) { gs.dead = 1; return; }
    if (cell_free((signed char)x + DX[e[0]], (signed char)y + DY[e[0]])) {
        move_to(x, y, e[0], e[1]); return;
    }
    if (e[2] != 0xFF && cell_free((signed char)x + DX[e[2]], (signed char)y + DY[e[2]])) {
        move_to(x, y, e[2], b); return;            /* keep state */
    }
    cave_set(x, y, e[3]); mark_dirty(x, y);        /* turn in place */
}

/* ---- bats: move horizontally, bounce, fire down ---- */
static void move_bat(unsigned char x, unsigned char y, unsigned char b) {
    unsigned char dir = (b == 0x4D) ? 2 : 3;       /* M=left, N=right */
    if (touches_robbo(x, y)) { gs.dead = 1; return; }
    if (cell_free((signed char)x + DX[dir], (signed char)y + DY[dir]))
        move_to(x, y, dir, b);
    else { cave_set(x, y, b == 0x4D ? 0x4E : 0x4D); mark_dirty(x, y); }  /* bounce */
    if ((rnd() & 7) == 0) do_fire(x, y, 1);            /* occasionally fire down */
}

/* ---- chaser (LEDZ): 50% random wander, 50% home (vertical first) ---- */
static void move_chaser(unsigned char x, unsigned char y) {
    unsigned char dir;
    if (touches_robbo(x, y)) { gs.dead = 1; return; }   /* TFAC: adjacency kills robbo */

    if (rnd() & 1) {                               /* 50%: random wander (SKAC) */
        if ((rnd() & 0x20) == 0) {                 /* ...and ~half of those actually step */
            dir = rnd() & 3;
            if (cell_free((signed char)x + DX[dir], (signed char)y + DY[dir]))
                move_to(x, y, dir, 0x26);
        }
        return;
    }

    /* 50%: home — try vertical toward robbo first, then horizontal */
    if (gs.robbo_y != y) {
        dir = (gs.robbo_y < y) ? 0 : 1;
        if (cell_free((signed char)x + DX[dir], (signed char)y + DY[dir]))
            { move_to(x, y, dir, 0x26); return; }
    }
    if (gs.robbo_x != x) {
        dir = (gs.robbo_x < x) ? 2 : 3;
        if (cell_free((signed char)x + DX[dir], (signed char)y + DY[dir]))
            move_to(x, y, dir, 0x26);
    }
}

/* ---- magnets: '(' 0x28 (MGNL+MGFL) grabs Robbo from the RIGHT along its row
   and drags him LEFT into itself; ')' 0x29 (the mirror, MGFR) grabs him from the
   LEFT and drags him RIGHT.  With a clear line of sight it pulls him one cell/tick
   and marks him done so he can't act while held, crushing him on arrival.
   Run as a PRE-PASS (before the main sweep) so a right-pulling magnet - which sits
   to Robbo's right, i.e. later in scan order - still pre-empts his move. ---- */
static unsigned char mag_prev, mag_cur;    /* a magnet is dragging Robbo (one-shot sound) */
static void magnet_pull(unsigned char x, unsigned char y, signed char step) {
    unsigned char cx;                      /* step -1: drag left (0x28); +1: drag right (0x29) */
    if (gs.robbo_y != y) return;
    if (step < 0) {                                    /* Robbo must be to the right */
        if (gs.robbo_x <= x) return;
        for (cx = x + 1; cx < gs.robbo_x; cx++)
            if (cave_get(cx, y) != OB_SPACE) return;   /* line of sight blocked */
    } else {                                           /* Robbo must be to the left */
        if (gs.robbo_x >= x) return;
        for (cx = gs.robbo_x + 1; cx < x; cx++)
            if (cave_get(cx, y) != OB_SPACE) return;
    }
    if (!mag_prev) snd_play(SND_MAGNET);               /* one pull sound per grab */
    mag_cur = 1;
    if ((int)gs.robbo_x + step == (int)x) { gs.dead = 1; return; }  /* reached it -> crushed */
    /* drag Robbo one cell toward the magnet; mark done so he's helpless this tick */
    cave_set(gs.robbo_x, gs.robbo_y, OB_SPACE); mark_dirty(gs.robbo_x, gs.robbo_y);
    gs.robbo_x = (unsigned char)((int)gs.robbo_x + step);
    cave_set(gs.robbo_x, gs.robbo_y, OB_ROBBO); mark_dirty(gs.robbo_x, gs.robbo_y);
    mark(gs.robbo_x, gs.robbo_y);
}

static void magnets_update(void) {
    unsigned int i;
    for (i = 0; i < LVL_W * LVL_H; i++) {
        unsigned char b = cave[i];
        if (b == 0x28)      magnet_pull((unsigned char)(i % LVL_W), (unsigned char)(i / LVL_W), -1);
        else if (b == 0x29) magnet_pull((unsigned char)(i % LVL_W), (unsigned char)(i / LVL_W), +1);
        if (gs.dead) return;
    }
}

/* ---- DZRU rotating cannon ',-./' (0x2C-0x2F): rotates its aim, fires sometimes ---- */
static unsigned char is_dzru(unsigned char b) { return b >= 0x2C && b <= 0x2F; }
static const unsigned char DZRU_DIR[4] = { 3, 1, 2, 0 };  /* ,=R -=D .=L /=U  (index b-0x2C) */
static void move_dzru(unsigned char x, unsigned char y, unsigned char b) {
    if ((tickctr & 3) == 0 && (rnd() & 3) == 0) {     /* ~1/16 tick: rotate the aim glyph */
        unsigned char g;
        if (rnd() & 1) g = (b >= 0x2F) ? 0x2C : b + 1;     /* forward, wrap ','..'/' */
        else           g = (b <= 0x2C) ? 0x2F : b - 1;     /* backward, wrap */
        cave_set(x, y, g); mark_dirty(x, y);
        return;
    }
    if ((rnd() & 7) == 0)                              /* ~1/8: fire a bullet in its facing */
        do_fire(x, y, DZRU_DIR[b - 0x2C]);
}

/* ---- MOD moving cannon (0x0D left, 0x0E right): crawls along, also fires up ---- */
static void move_mod(unsigned char x, unsigned char y, unsigned char b) {
    unsigned char dir = (b == 0x0D) ? 2 : 3;
    if ((tickctr & 1) &&                              /* odd tick: try to slide one cell */
        cell_free((signed char)x + DX[dir], (signed char)y + DY[dir])) {
        move_to(x, y, dir, b);
        x = (unsigned char)((signed char)x + DX[dir]);
        y = (unsigned char)((signed char)y + DY[dir]);
    }
    fire_blaster(x, y, 0);                            /* falls through to a DZ1U cannon (fire up) */
}

/* ---- ZAPO force-field (0x11): pulses a barrier of field cells (0x0F) rightward
   along its row to a 0x05 terminator; the field is lethal while it is "on" ---- */
#define OB_FIELD   0x0F
#define OB_ZAPEND  0x05
static void move_zapo(unsigned char x, unsigned char y) {
    unsigned char on = (tickctr >> 2) & 1;            /* ~4 ticks on, 4 off */
    unsigned char cx;
    for (cx = x + 1; cx < LVL_W; cx++) {
        unsigned char c = cave_get(cx, y);
        if (c == OB_ZAPEND) break;                    /* end of the field run */
        if (on) {
            if (at_robbo((signed char)cx, (signed char)y)) { gs.dead = 1; return; }
            if (c == OB_SPACE)      { cave_set(cx, y, OB_FIELD); mark_dirty(cx, y); }
            else if (c != OB_FIELD) break;            /* solid obstacle ends the run */
        } else {
            if (c == OB_FIELD)      { cave_set(cx, y, OB_SPACE); mark_dirty(cx, y); }
            else if (c != OB_SPACE) break;
        }
    }
}

/* ---- inertia: pushed objects slide until blocked ---- */
#define MAX_SLIDE 6
static struct { unsigned char x, y, dir, b, active; } slide[MAX_SLIDE];

void slide_add(unsigned char x, unsigned char y, unsigned char dir, unsigned char b) {
    unsigned char i;
    for (i = 0; i < MAX_SLIDE; i++)
        if (!slide[i].active) { slide[i].x = x; slide[i].y = y; slide[i].dir = dir;
                                slide[i].b = b; slide[i].active = 1; return; }
}
static void slide_update(void) {
    unsigned char i;
    for (i = 0; i < MAX_SLIDE; i++) {
        if (!slide[i].active) continue;
        {
            signed char nx = (signed char)slide[i].x + DX[slide[i].dir];
            signed char ny = (signed char)slide[i].y + DY[slide[i].dir];
            if (cell_free(nx, ny)) {
                cave_set(slide[i].x, slide[i].y, OB_SPACE); mark_dirty(slide[i].x, slide[i].y);
                slide[i].x = (unsigned char)nx; slide[i].y = (unsigned char)ny;
                cave_set(slide[i].x, slide[i].y, slide[i].b); mark_dirty(slide[i].x, slide[i].y);
            } else {
                slide[i].active = 0;                   /* came to rest */
            }
        }
    }
}

/* active_tbl[b] != 0 for object bytes that have per-tick behaviour.  The scan
   uses this one-byte lookup to skip inert cells (space, wall, screws, ground,
   teleports, doors, ...) instead of running the whole dispatch chain on them. */
static unsigned char active_tbl[256];
void objects_init(void) {
    unsigned int b;
    for (b = 0; b < 256; b++)
        active_tbl[b] =
            (IS_BULLET(b) || IS_DZ3_BULLET(b) || IS_ANIM(b) || IS_STW(b) || IS_BAT(b)
             || b == OB_ROBBO || b == 0x26 || b == 0x60
             || b == OB_EXIT || b == 0x16
             || b == 0x0D || b == 0x0E || b == 0x11 || is_dzru((unsigned char)b)
             || is_laser((unsigned char)b) || is_dz3((unsigned char)b)
             || blaster_dir((unsigned char)b) >= 0) ? 1 : 0;
}

/* Clear transient cross-tick state (active beams, sliding crates) on level load
   so stale entries from the previous level don't paint into the new one. */
void objects_reset(void) {
    unsigned char i;
    for (i = 0; i < MAX_LASER; i++) lbeam[i].active = 0;
    for (i = 0; i < MAX_SLIDE; i++) slide[i].active = 0;
    mag_prev = mag_cur = 0;
}

void objects_update(unsigned char keys) {
    unsigned char x, y, exit_on;
    unsigned int i;
    signed char bd;
    if (++done_gen == 0) {                      /* generation wrapped: clear once */
        for (i = 0; i < LVL_W * LVL_H; i++) done[i] = 0;
        done_gen = 1;
    }

    tickctr++;
    exit_on  = (tickctr >> 1) & 1;   /* capsule blink phase */
    mag_prev = mag_cur; mag_cur = 0; /* magnet grab: edge-detect for one pull sound */

    magnets_update();                /* pre-pass: a magnet grabs/drags Robbo before he can act */
    if (gs.dead) return;
    slide_update();
    laser_update();                  /* extend/retract every live beam, then redraw */

    i = 0;
    for (y = 0; y < LVL_H; y++) {
        for (x = 0; x < LVL_W; x++, i++) {
            unsigned char b = cave[i];
            /* one-byte lookup skips all inert cells before the dispatch chain
               and the done[] check - this is the bulk of the scan cost. */
            if (!active_tbl[b]) continue;
            if (done[i] == done_gen) continue;
            if (b == OB_ROBBO)      player_act(x, y, keys);  /* Robbo in scan order */
            else if (IS_BULLET(b))  move_bullet(x, y, b);
            else if (IS_DZ3_BULLET(b)) move_dz3_bullet(x, y, b);
            else if (IS_ANIM(b))    advance_anim(x, y, b); /* explosion a..g -> space */
            else if (IS_STW(b))     move_stw(x, y, b);
            else if (IS_BAT(b))     move_bat(x, y, b);
            else if (b == 0x26)     move_chaser(x, y);
            else if (b == 0x60)     bomb_explode(x, y);    /* triggered bomb */
            else if (b == OB_EXIT || b == 0x16) {          /* capsule: blink once open */
                if (gs.exit_open) {
                    unsigned char want = exit_on ? 0x16 : OB_EXIT;
                    if (b != want) { cave_set(x, y, want); mark_dirty(x, y); }
                }
            }
            else if (is_laser(b))   laser_seed(x, y, laser_dir(b));
            else if (is_dz3(b))     fire_dz3(x, y, dz3_dir(b));
            else if (is_dzru(b))    move_dzru(x, y, b);
            else if (b == 0x0D || b == 0x0E) move_mod(x, y, b);
            else if (b == 0x11)     move_zapo(x, y);
            else if ((bd = blaster_dir(b)) >= 0) fire_blaster(x, y, (unsigned char)bd);
            if (gs.dead || gs.won) return;
        }
    }
}
