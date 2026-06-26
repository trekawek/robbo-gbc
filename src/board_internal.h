/* Prototypes shared between board.c (HOME) and board_upd.c (banked).
   The upd_g* group handlers live in a switchable bank and are called from
   update_game() via the __banked trampoline.  The helper prototypes mirror
   board.c's own forward declarations so board_upd.c can call them (they live in
   HOME, always mapped, so they are NOT __banked). */
#ifndef BOARD_INTERNAL_H
#define BOARD_INTERNAL_H

#define MAX_TELEPORT_IDS 15   /* used by move_robbo (now in the banked module) */

/* GBC perf: a cell is "active" (must be visited by update_game) iff it has an
   object type with behaviour, OR a pending move/blow delay.  We mark such cells
   with the inlist bit so the per-cell scan can skip inert cells with one test.
   gr_act_type[t] != 0 for types that have a case in update_game's switch.
   Every assignment to .moved / .blowed goes through these setters so the cell
   is (re)activated; update_game clears inlist when a cell goes inert again. */
extern const unsigned char gr_act_type[71];
/* Set a cell's moved/blowed delay and (re)activate it.  These live in HOME
   (board.c) and are called from the banked modules too - keeping them out of
   line stops the ~50 SET_MOVED sites in board_upd.c from overflowing its bank. */
void gr_set_moved(int x, int y, int v);
void gr_set_blowed(int x, int y, int v);
#define SET_MOVED(cx, cy, v)  gr_set_moved((cx), (cy), (v))
#define SET_BLOWED(cx, cy, v) gr_set_blowed((cx), (cy), (v))

int  random_id(void) __banked;
int  update_coords(struct Coords *coords, int direction);
int  can_move(struct Coords coords, int direction);
void move_object(int x, int y, struct Coords coords);
void shoot_object(int x, int y, int direction);
void blow_bomb(int x, int y) __banked;
void blow_bomb2(int x, int y) __banked;
void check_object_if_blowed(int x, int y);
int  find_teleport(struct Coords *coords, int teleportnumber, int teleportnumber2) __banked;
int  is_robbo_killed(void);
void kill_robbo(void);

/* banked group handlers (defined in board_upd.c) */
int upd_g1(int x, int y) __banked;
int upd_g2(int x, int y) __banked;
int upd_g3(int x, int y) __banked;
int upd_g4(int x, int y) __banked;
int upd_g5(int x, int y) __banked;

#endif
