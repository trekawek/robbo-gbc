/*  GNU Robbo board.h - GBC port
 *  Original: Copyright (C) 2002-2010 The GNU Robbo Team (see AUTHORS). GPLv2+.
 *
 *  GBC changes vs upstream:
 *   - MAX_W reduced to 16 (original.dat levels are 16 wide); GB RAM budget.
 *   - struct object: int fields packed into bitfields/chars and the SDL-only
 *     icon[MAX_ICONS] coordinate cache removed, so the array fits GB WRAM
 *     (10 bytes/cell -> board[16][31] ~= 5KB).  Field NAMES are unchanged so
 *     board.c's logic compiles verbatim.
 *   - Globals declared via GR_GLOBAL (extern everywhere, defined once in the
 *     GR_DEFINE_GLOBALS owner TU) because SDCC has no -fcommon.
 */
#ifndef BOARD_H
#define BOARD_H

#ifndef GR_GLOBAL
#  ifdef GR_DEFINE_GLOBALS
#    define GR_GLOBAL
#  else
#    define GR_GLOBAL extern
#  endif
#endif

/* Defines */
#define MAX_W 16		/* max width of the board (GBC: 16, was 32) */
#define MAX_H 31		/* max height of the board */
#define MAX_ICONS 9

#define DEFAULT_VIEWPORT_WIDTH 16
#define DEFAULT_VIEWPORT_HEIGHT 12

/* Two engine ticks represent one Atari world step (seven PAL VBlanks).
   Keep the inherited GNU Robbo delay ratios; SCALE() floors at one tick. */
#define GR_DELAY_DIV 2
#define SCALE(x) ((x) / GR_DELAY_DIV < 1 ? 1 : (x) / GR_DELAY_DIV)

/* Fractional ticks per GBC VBlank. Atari CHNGCV reloads its timer with 7;
   DELAY_ROBBO=2 means one tick must last 3.5 PAL frames, about 70.1955 ms.
   5488/23009 approximates the exact clock ratio 500203/2097152 with less
   than 0.00002% error. A fixed 4- or 5-frame gate would drift from PAL. */
#define GR_PAL_PHASE_STEP 5488u
#define GR_PAL_PHASE_PERIOD 23009u
#define DELAY_RADIOACTIVE_FIELD  SCALE(3)
#define DELAY_BIRD SCALE(4)
#define DELAY_LITTLE_BOOM SCALE(2)
#define DELAY_BEAR SCALE(4)
#define DELAY_BUTTERFLY SCALE(4)
#define DELAY_BIGBOOM SCALE(3)
#define DELAY_ROBBO SCALE(4)
#define DELAY_TELEPORT SCALE(8)
#define DELAY_LASER SCALE(4)
#define DELAY_CAPSULE SCALE(10)
#define DELAY_GUN SCALE(8)
#define DELAY_ROTATION SCALE(20)
#define DELAY_BLASTER SCALE(4)
#define DELAY_PUSHBOX SCALE(4)
#define DELAY_BARRIER SCALE(4)
#define DELAY_TELEPORTING SCALE(3)
#define DELAY_BOMB_TARGET (DELAY_BIGBOOM + 2)
#define DELAY_ROBBO_ANIMATE (DELAY_ROBBO / 2)
/* Atari checks a magnet once per world step, then moves a captured Robbo on
   every second step: the inverted magnetized glyph consumes the other step. */
#define DELAY_MAGNET_SCAN DELAY_ROBBO
#define DELAY_MAGNET_ATTRACT (DELAY_ROBBO * 2)
#define DELAY_BLINKSCREEN SCALE(6)
#define DELAY_RESTART (DELAY_BIGBOOM * 8)

/* Object ids */
#define EMPTY_FIELD 0
#define ROBBO 1
#define WALL 2
#define WALL_RED 3
#define SCREW 4
#define BULLET 5
#define BOX 6
#define KEY 7
#define BOMB 8
#define DOOR 9
#define QUESTIONMARK 10
#define BEAR 11
#define BIRD 13
#define CAPSULE 15
#define LITTLE_BOOM 21
#define GROUND 24
#define WALL_GREEN 25
#define BEAR_B 26
#define BUTTERFLY 28
#define LASER_L 30
#define LASER_D 32
#define SOLID_LASER_L 34
#define SOLID_LASER_D 36
#define TELEPORT 40
#define TELEPORTING 41
#define BIG_BOOM 42
#define GUN 50
#define MAGNET 54
#define BLASTER 58
#define BLACK_WALL 59
#define PUSH_BOX 60
#define BARRIER 61
#define FAT_WALL 63
#define ROUND_WALL 64
#define BOULDER_WALL 65
#define SQUARE_WALL 66
#define LATTICE_WALL 67
#define RADIOACTIVE_FIELD  68
#define STOP 69
#define BOMB2 70
#define SCORE_BONUS 71  /* Atari extra-life glyph; awards points, not lives */

#define MECHANIC_SENSIBLE_BEARS TRUE
#define MECHANIC_SENSIBLE_QUESTIONMARKS TRUE
#define MECHANIC_SENSIBLE_SOLID_LASERS TRUE

/* Variables */
GR_GLOBAL int restart_timeout;	/* Time to wait before restarting a level after Robbo dies */

struct Coords
{
  int x;
  int y;
};

/* GBC: packed object cell (14 bytes -> board[16][31] ~= 7KB, fits WRAM).
   Hybrid layout: int-valued fields are plain bytes (compact, fast code on the
   sm83), only the booleans are bitfields.  icon[] (SDL) removed.  Field names
   match upstream so board.c compiles unchanged. */
struct object
{
  unsigned char type;		/* object id (0..70) */
  unsigned char id_questionmark;/* What object is covered under questionmark (a type) */
  unsigned char state;		/* animation/state index */
  unsigned char direction;	/* robbo uses 0,2,4,6; objects 0..3 */
  unsigned char direction2;	/* direction of moving (guns/birds) */
  unsigned char teleportnumber;	/* kind of teleport */
  unsigned char teleportnumber2;/* id of teleport within a kind */
  unsigned char solidlaser;	/* gun shoots solid(1)/normal(0)/blaster(2) */
  unsigned char moved;		/* delay countdown till next move */
  unsigned char shooted;	/* delay countdown till next shot (guns) */
  unsigned char rotated;	/* delay countdown till next rotate (guns) */
  unsigned char processed;	/* low byte of cycle_count once processed */
  unsigned char destroyable : 1;/* can be destroyed by a shot/push box */
  unsigned char blowable : 1;	/* can be blown up by a bomb */
  unsigned char killing : 1;	/* dangerous for robbo by closeness */
  unsigned char blowed : 1;	/* should object be blown up? */
  unsigned char rotable : 1;	/* can be rotated (guns) */
  unsigned char randomrotated : 1;/* undetermined rotation */
  unsigned char movable : 1;	/* is object moving (guns) */
  unsigned char returnlaser : 1;/* only for solid lasers */
  unsigned char shooting : 1;	/* if birds can shoot */
  unsigned char redraw : 1;	/* needs redrawing in show_game_area() */
  unsigned char inlist : 1;	/* GBC perf: cell has per-cycle behaviour (active);
				   update_game skips !inlist cells with one bit test
				   instead of re-deriving inertness every cycle */
};
GR_GLOBAL struct object board[MAX_W][MAX_H];	/* The game area, indexed [x][y] */

/* GBC perf: per-row count of active (inlist=1) cells.  update_game skips whole
   rows with zero active cells (most of the board) instead of walking all 496
   cells every cycle. Every inlist transition updates this exact count:
   creation/delay setters activate; replacement/clearing/retirement remove.
   This includes moves into cells already visited, without a second row scan.
   The y-then-x gameplay scan order is unchanged. */
GR_GLOBAL unsigned char gr_row_active[MAX_H];

GR_GLOBAL struct
{
  int x;			/* Board x position */
  int y;			/* Board y position */
  int alive;			/* if Robbo is alive */
  int state;			/* Robbo's state (0 or 1) */
  int direction;		/* Robbo's direction 0,2,4,6 + state => icon */
  int screws;			/* The initial number of screws to collect */
  int keys;			/* Keys collected */
  int bullets;			/* Bullets collected */
  int moved;			/* A delay countdown till next move */
  int shooted;			/* A delay countdown till next shot */
  int exitopened;		/* TRUE when all required screws collected */
  int blocked;			/* robbo cannot move - magnet moving */
  int blocked_direction;	/* where robbo should be moved after blocking */
  int teleporting;		/* TRUE when Robbo is teleporting */
} robbo;

/* What is shown of the board is seen through this viewport */
GR_GLOBAL struct
{
  int x;			/* Board x position */
  int y;			/* Board y position */
  int w;
  int h;
  int max_w;
  int max_h;
  int xoffset;
  int yoffset;
  int cycles_to_dest;
  int maximise;
} viewport;

/* Game mechanics modifiable via rcfile (GBC: fixed defaults) */
GR_GLOBAL struct
{
  int sensible_bears;
  int sensible_questionmarks;
  int sensible_solid_lasers;
} game_mechanics;

/* Function prototypes */
void update_game (void);
void init_questionmarks (void) __banked;
void open_exit (void);
void init_robbo (void);
void move_robbo (int x, int y) __banked;
void shoot_robbo (int x, int y) __banked;
void viewport_needs_redrawing (void);
void create_object (int x, int y, int type);
void clear_entire_board (void);
void set_coords (struct Coords *coords, int x, int y);
int coords_out_of_range (struct Coords coords);
void negate_state (int x, int y);
void redraw_field (int x, int y);
void clear_field (int x, int y);
int in_viewport(int x, int y);

#endif
