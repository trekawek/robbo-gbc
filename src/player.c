#include <gb/gb.h>
#include "player.h"
#include "game.h"
#include "cave.h"
#include "render.h"
#include "object_tables.h"
#include "objects.h"
#include "sound.h"

/* player bullet object bytes (pocis 1 l/r/u/d) — moved by objects_update() */
const unsigned char BULLET[4] = { 0x51, 0x52, 0x4F, 0x50 }; /* up,down,left,right */

#define OB_BEZ  0x06   /* inertial sliding crate (rest state) */

static unsigned char is_collectible(unsigned char b) {
    return b == OB_SCREW || b == OB_AMMO || b == OB_KEY || b == OB_LIFE;
    /* '?' (OB_QUEST) is NOT collectible in the original - it's a pushable capsule
       that yields loot only when shot.  OB_GROUND '%' is debris (shoot to clear). */
}

static void collect(unsigned char b) {
    switch (b) {
        case OB_SCREW:
            if (gs.screws) gs.screws--;
            gs.score += 8;
            if (gs.screws == 0) { gs.exit_open = 1; snd_play(SND_CAPSULE); }  /* exit opens */
            else snd_play(SND_SCREW);
            break;
        case OB_AMMO:
            if (gs.ammo <= 90) gs.ammo += 9; else gs.ammo = 99;   /* +9 cap 99 */
            gs.score += 5;
            snd_play(SND_AMMO);
            break;
        case OB_KEY:
            if (gs.keys < 99) gs.keys++;
            gs.score += 5;
            snd_play(SND_KEY);
            break;
        case OB_LIFE:
            if (gs.lives < 99) gs.lives++;
            snd_play(SND_LIFE);
            break;
        default: break;
    }
}

static void robbo_to(unsigned char nx, unsigned char ny) {
    cave_set(gs.robbo_x, gs.robbo_y, OB_SPACE);
    mark_dirty(gs.robbo_x, gs.robbo_y);
    gs.robbo_x = nx; gs.robbo_y = ny;
    cave_set(nx, ny, OB_ROBBO);
    mark_dirty(nx, ny);
    obj_mark(nx, ny);          /* Robbo just arrived: don't re-process him this sweep */
}

/* Teleport, with the original's animation (R1/R2 NEXT chains): Robbo collapses
   into a shrinking burst at the source ('d'->'e'->'f'->'g'->space) while a burst
   grows into Robbo at the destination ('i'->'j'->'k'->'l'->'m'->'*').  Both are
   advanced one frame per tick by advance_anim; Robbo is frozen (the cell is
   'i'..'m', not '*') until the chain completes - matching RUCH=0 in the original. */
#define TELE_OUT  0x64   /* 'd' - first dissolve frame */
#define TELE_IN   0x69   /* 'i' - first materialise frame */
static void teleport_to(unsigned char ox, unsigned char oy) {
    snd_play(SND_TELEPORT);
    cave_set(gs.robbo_x, gs.robbo_y, TELE_OUT);   /* source dissolves away */
    mark_dirty(gs.robbo_x, gs.robbo_y);
    gs.robbo_x = ox; gs.robbo_y = oy;
    cave_set(ox, oy, TELE_IN);                    /* destination materialises into Robbo */
    mark_dirty(ox, oy);
    obj_mark(ox, oy);                             /* hold frame 'i' this tick, advance from next */
    render_snap(ox, oy);                          /* centre the materialisation */
}

static void player_move(unsigned char dir) {
    signed char tx, ty, bx, by;
    unsigned char t, beyond;

    gs.facing = dir;
    tx = (signed char)gs.robbo_x + DX[dir];
    ty = (signed char)gs.robbo_y + DY[dir];
    if (tx < 0 || tx >= LVL_W || ty < 0 || ty >= LVL_H) return;
    t = cave_get(tx, ty);

    if (t == OB_SPACE) {
        robbo_to(tx, ty);
        return;
    }
    if (is_collectible(t)) {
        collect(t);
        robbo_to(tx, ty);
        return;
    }
    if (t == OB_DOOR_H || t == OB_DOOR_V) {
        if (gs.keys) {
            gs.keys--;
            snd_play(SND_DOOR);
            robbo_to(tx, ty);   /* door opens, robbo steps in */
        }
        return;
    }
    if (t == OB_BOX || t == OB_BOMB || t == OB_QUEST) {  /* single-cell push */
        bx = tx + DX[dir]; by = ty + DY[dir];
        if (bx < 0 || bx >= LVL_W || by < 0 || by >= LVL_H) return;
        beyond = cave_get(bx, by);
        if (beyond == OB_SPACE) {
            cave_set(bx, by, t); mark_dirty(bx, by);
            snd_play(SND_PUSH);
            robbo_to(tx, ty);
        }
        return;
    }
    if (t == OB_BEZ) {                  /* inertial crate: shove it and let it slide */
        bx = tx + DX[dir]; by = ty + DY[dir];
        if (bx < 0 || bx >= LVL_W || by < 0 || by >= LVL_H) return;
        beyond = cave_get(bx, by);
        if (beyond == OB_SPACE) {
            cave_set(bx, by, OB_BEZ); mark_dirty(bx, by);
            slide_add(bx, by, dir, OB_BEZ);   /* keeps moving until blocked */
            snd_play(SND_PUSH);
            robbo_to(tx, ty);
        }
        return;
    }
    if (t == OB_EXIT || t == 0x16) {     /* exit capsule (0x16 = its blink frame) */
        if (gs.exit_open) {
            gs.won = 1;
            snd_play(SND_WIN);
        }
        return;
    }
    if (t >= 0x30 && t <= 0x39) {        /* teleport pad */
        unsigned int i;
        unsigned char paired = 0;
        for (i = 0; i < LVL_W * LVL_H; i++) {
            unsigned char qx = i % LVL_W, qy = i / LVL_W;
            if (cave[i] == t && !(qx == tx && qy == ty)) {   /* the remote pad */
                unsigned char d;
                signed char ox, oy;
                paired = 1;
                /* exit on the far side of the remote pad in Robbo's travel
                   direction (enter from the left -> appear on the right) */
                ox = (signed char)qx + DX[dir]; oy = (signed char)qy + DY[dir];
                if (ox >= 0 && ox < LVL_W && oy >= 0 && oy < LVL_H &&
                    cave_get((unsigned char)ox, (unsigned char)oy) == OB_SPACE) {
                    teleport_to((unsigned char)ox, (unsigned char)oy);
                    return;
                }
                for (d = 0; d < 4; d++) {            /* else any free side of the remote pad */
                    ox = (signed char)qx + DX[d]; oy = (signed char)qy + DY[d];
                    if (ox >= 0 && ox < LVL_W && oy >= 0 && oy < LVL_H &&
                        cave_get((unsigned char)ox, (unsigned char)oy) == OB_SPACE) {
                        teleport_to((unsigned char)ox, (unsigned char)oy);
                        return;
                    }
                }
            }
        }
        if (!paired) {                   /* singular pad: pass straight through to the far side */
            signed char fx = (signed char)tx + DX[dir];
            signed char fy = (signed char)ty + DY[dir];
            if (fx >= 0 && fx < LVL_W && fy >= 0 && fy < LVL_H &&
                cave_get((unsigned char)fx, (unsigned char)fy) == OB_SPACE)
                teleport_to((unsigned char)fx, (unsigned char)fy);
        }
        return;
    }
    if (t >= 0x41 && t <= 0x4E) {        /* walked into a monster ('A'..'N') -> death */
        gs.dead = 1;
        return;
    }
    if (t == 0x0F) {                     /* walked into a live force field -> death */
        gs.dead = 1;
        return;
    }
    /* anything else (wall, blaster, bullet) blocks */
}

static void player_fire(unsigned char dir) {
    signed char tx, ty;
    unsigned char t;
    gs.facing = dir;
    gs.ammo--;                        /* ammo already checked > 0 by caller */
    snd_play(SND_SHOOT);
    tx = (signed char)gs.robbo_x + DX[dir];
    ty = (signed char)gs.robbo_y + DY[dir];
    if (tx < 0 || tx >= LVL_W || ty < 0 || ty >= LVL_H) return;
    t = cave_get(tx, ty);
    if (t == OB_SPACE) {
        cave_set(tx, ty, BULLET[dir]);    /* bullet sits next to Robbo, flies next tick */
        mark_dirty(tx, ty);
        obj_mark(tx, ty);
    } else {
        obj_bullet_hit(tx, ty, t);        /* point-blank: destroy/detonate what's adjacent */
    }
}

/* MFAC: process Robbo this tick.  Fire button held -> shoot (never move),
   gated by a 5-tick cooldown; otherwise move/push in the held direction. */
void player_act(unsigned char cx, unsigned char cy, unsigned char keys) {
    static signed char strc;          /* fire cooldown, in ticks */
    signed char dir = -1;
    (void)cx; (void)cy;               /* Robbo's position lives in gs */

    if (keys & J_UP)        dir = 0;  /* priority up > down > left > right */
    else if (keys & J_DOWN) dir = 1;
    else if (keys & J_LEFT) dir = 2;
    else if (keys & J_RIGHT) dir = 3;

    if (strc > 0) strc--;

    gs.moving = 0;
    if (keys & (J_A | J_B)) {                       /* fire held: shoot, do not walk */
        if (strc == 0 && dir >= 0 && gs.ammo) {
            player_fire((unsigned char)dir);
            strc = 5;                               /* auto-repeat every 5 ticks */
        }
    } else if (dir >= 0) {
        player_move((unsigned char)dir);
        gs.moving = 1;
    }
}
