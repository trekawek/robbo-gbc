/* GBC level loader: runs gnu-robbo's transform_char + load_level_data [data]/
   [additional] logic (verbatim) against the baked ROM arrays in levels_data.c
   instead of fgets from a .dat file. */
#include "game.h"
#include "levels_data.h"

/* Debug test level.  Set to 1 to replace EVERY level with a tiny board -
   Robbo, a screw, then the exit capsule in a row - for exercising the
   screw -> exit-open -> capsule -> next-level path in isolation.  0 = off.
   Override without editing: `make LCCFLAGS_EXTRA=-DTEST_LEVEL=1`. */
#ifndef TEST_LEVEL
#define TEST_LEVEL 0
#endif

/* verbatim from gnu-robbo levels.c */
int transform_char(char c)
{
    switch (c) {
    case 'X': return STOP;
    case 'k': return RADIOACTIVE_FIELD;
    case '.': return EMPTY_FIELD;
    case 'R': return ROBBO;
    case 'O': return WALL;
    case 'Q': return WALL_RED;
    case 'T': return SCREW;
    case '\'': return BULLET;
    case '#': return BOX;
    case '%': return KEY;
    case 'B': return BOMB2;
    case 'b': return BOMB;
    case 'D': return DOOR;
    case '?': return QUESTIONMARK;
    case '@': return BEAR;
    case '^': return BIRD;
    case '!': return CAPSULE;
    case 'H': return GROUND;
    case 'o': return WALL_GREEN;
    case '*': return BEAR_B;
    case 'V': return BUTTERFLY;
    case '&': return TELEPORT;
    case '}': return GUN;
    case 'M': return MAGNET;
    case '-': return BLACK_WALL;
    case '~': return PUSH_BOX;
    case '=': return BARRIER;
    case 'q': return FAT_WALL;
    case 'p': return ROUND_WALL;
    case 'P': return BOULDER_WALL;
    case 's': return SQUARE_WALL;
    case 'S': return LATTICE_WALL;
    case '+': return EMPTY_FIELD;   /* extra life unsupported */
    case 'L': return LASER_L;
    case 'l': return LASER_D;
    default:  return EMPTY_FIELD;
    }
}

/* Reads baked level (1-based number) into board[][]; mirrors load_level_data's
   [data] and [additional] switches.  Returns 0 on success, 1 if missing. */
int load_level_data(int level_number)
{
    int idx = level_number - 1;
    const unsigned char *grid, *add;
    int w, h, x, y, t, n, r;

#if TEST_LEVEL
    /* Level 1 only: tiny debug board (walls all around, row 1 = Robbo, screw,
       capsule, gap).  Completing it advances into the REAL level 2, so this
       exercises the screw -> exit-open -> capsule -> next-(real-)level path.
       Levels 2+ load their normal data below. */
    if (level_number == 1) {
        level.w = 6; level.h = 3; level.colour = 0;
        for (y = 0; y < 3; y++) {
            for (x = 0; x < 6; x++) {
                t = WALL;
                if (y == 1) {
                    if (x == 1) t = ROBBO;
                    else if (x == 2) t = SCREW;
                    else if (x == 3) t = CAPSULE;
                    else if (x == 4) t = EMPTY_FIELD;
                }
                board[x][y].type = t;
                create_object(x, y, t);
                if (t == SCREW) robbo.screws++;
            }
        }
        return FALSE;
    }
#endif

#if ENDING_TEST
    /* Bonus test planet = level GR_NLEVELS+1 (the last level).  Trivial: step
       right onto the screw (opens the exit), then right onto the capsule to win
       and trigger the final animation.  `make ENDING_TEST=1`; warp to the top. */
    if (level_number == GR_NLEVELS + 1) {
        level.w = 6; level.h = 3; level.colour = 0x606060;
        for (y = 0; y < 3; y++) for (x = 0; x < 6; x++) {
            t = WALL;
            if (y == 1) {
                if      (x == 1) t = ROBBO;
                else if (x == 2) t = SCREW;
                else if (x == 3) t = CAPSULE;
                else if (x == 4) t = EMPTY_FIELD;
            }
            board[x][y].type = t;
            create_object(x, y, t);
            if (t == SCREW) robbo.screws++;
        }
        return FALSE;
    }
#endif

    if (idx < 0 || idx >= GR_NLEVELS) return TRUE;
    w = gr_level_w[idx];
    h = gr_level_h[idx];
    if (w == 0 || h == 0) return TRUE;
    gr_level_bankswitch(idx);          /* map the ROM bank holding this level's data */
    grid = gr_level_grid[idx];

    level.w = w;
    level.h = h;
    level.colour = gr_level_colour[idx];

    /* ---- [data] ---- */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            t = transform_char((char)grid[y * w + x]);
            board[x][y].type = t;
            create_object(x, y, t);
            switch (t) {
            case SCREW:        robbo.screws++; break;
            case WALL_RED:     create_object(x, y, WALL); board[x][y].state = 1; break;
            case WALL_GREEN:   create_object(x, y, WALL); board[x][y].state = 2; break;
            case BLACK_WALL:   create_object(x, y, WALL); board[x][y].state = 3; break;
            case FAT_WALL:     create_object(x, y, WALL); board[x][y].state = 4; break;
            case ROUND_WALL:   create_object(x, y, WALL); board[x][y].state = 5; break;
            case BOULDER_WALL: create_object(x, y, WALL); board[x][y].state = 6; break;
            case SQUARE_WALL:  create_object(x, y, WALL); board[x][y].state = 7; break;
            case LATTICE_WALL: create_object(x, y, WALL); board[x][y].state = 8; break;
            }
        }
    }

    /* ---- [additional] ---- record: x,y,sym,valcount,v0..v5 (10 bytes) ---- */
    add = gr_level_add[idx];
    n = gr_level_addn[idx];
    for (r = 0; r < n; r++) {
        const unsigned char *rec = add + r * 10;
        int rx = rec[0], ry = rec[1], valcount = rec[3];
        int v0 = rec[4], v1 = rec[5], v2 = rec[6], v3 = rec[7], v4 = rec[8], v5 = rec[9];
        if (rx < 0 || rx >= MAX_W || ry < 0 || ry >= MAX_H) continue;
        switch (transform_char((char)rec[2])) {
        case WALL:
            /* Atari-only presentation variants; collision stays WALL. */
            if (v0 == 9 || v0 == 10) board[rx][ry].state = v0;
            break;
        case LASER_L:
        case LASER_D:
            board[rx][ry].direction = v0;
            break;
        case TELEPORT:
            board[rx][ry].teleportnumber = v0;
            board[rx][ry].teleportnumber2 = v1;
            break;
        case GUN:
            board[rx][ry].direction = v0;
            board[rx][ry].state = v0;
            board[rx][ry].direction2 = v1;
            board[rx][ry].solidlaser = v2;
            /* SDCC mis-compiles `bitfield = int_var` here (the read-modify-write
               on board[rx][ry]'s flag byte silently no-ops), so movable/rotable/
               randomrotated never got set - rotating & moving guns stayed inert.
               Assigning a CONSTANT 1 (as create_object does) works, so set the bit
               conditionally.  The flags are already 0 from create_object. */
            if (v3) board[rx][ry].movable = 1;
            if (v4) board[rx][ry].rotable = 1;
            if (v5) board[rx][ry].randomrotated = 1;
            if (board[rx][ry].movable == 1)
                board[rx][ry].state += 4;
            break;
        case MAGNET:
            board[rx][ry].state = v0;
            if (valcount > 4 && v1) board[rx][ry].rotable = 1;   /* see GUN note */
            /* fall through (as upstream) */
        case BARRIER:
            board[rx][ry].direction = v0;
            break;
        case BIRD:
            board[rx][ry].direction2 = v1;
            if (v2) board[rx][ry].shooting = 1;                  /* see GUN note */
            /* fall through (as upstream) */
        case BEAR_B:
        case BEAR:
            board[rx][ry].direction = v0;
            break;
        }
    }

    return FALSE;
}

/* mirrors levels.c level_init() essentials (no SDL viewport math) */
int level_init(void)
{
    init_robbo();
    level.w = 0;
    level.h = 0;
    level.now_is_blinking = 0;
    level.colour = 0;

    clear_entire_board();

    if (load_level_data(level_packs[selected_pack].level_selected))
        return TRUE;

    if (robbo.screws == 0)
        open_exit();

    init_questionmarks();

    /* GBC: board.c uses viewport only for in_viewport()/viewport_needs_redrawing;
       cover the whole board (the GBC renderer does the real camera). */
    viewport.x = 0;
    viewport.y = 0;
    viewport.w = level.w;
    viewport.h = level.h;
    viewport.max_w = level.w;
    viewport.max_h = level.h;
    viewport.maximise = 0;
    viewport.cycles_to_dest = 0;

    return FALSE;
}
