/*  GBC port umbrella header - replaces gnu-robbo's SDL-based game.h.
 *  Provides exactly the types/defines/globals/prototypes that board.c (the
 *  verbatim gnu-robbo game logic) references, with no SDL dependency. */
#ifndef GR_GAME_H
#define GR_GAME_H

/* ---- global linkage (SDCC has no -fcommon: one owner TU defines) ---- */
#ifndef GR_GLOBAL
#  ifdef GR_DEFINE_GLOBALS
#    define GR_GLOBAL
#  else
#    define GR_GLOBAL extern
#  endif
#endif

/* ---- basic types/values ---- */
typedef unsigned long Uint32;
#define TRUE 1
#define FALSE 0
#define UNDEFINED -1

/* ---- game modes ---- */
#define INTRO_SCREEN 0
#define GAME_ON 1
#define END_SCREEN 2
#define HELP_SCREEN 3
#define OPTIONS_SCREEN 4
#define DESIGNER_ON 5

/* ---- sound (from sound.h) ---- */
#define SND_FULL 3
#define SND_NORM 2
#define SND_HALF 1
#define SND_MUTE 0
#define SND_QUIET 4
#define SFX_BULLET 1
#define SFX_SCREW 2
#define SFX_BOMB 3
#define SFX_BOX 4
#define SFX_DOOR 5
#define SFX_GUN 6
#define SFX_KEY 7
#define SFX_SHOOT 8
#define SFX_BIRD 9
#define SFX_TELEPORT 10
#define SFX_ROBBO 11
#define SFX_CAPSULE 12
#define SFX_KILL 13
#define SFX_MAGNET 14
#define SFX_EXIT_OPEN 15
#define SFX_KNOCK 16
/* music is unsupported on GBC: no-op like sound.h without HAVE_MUSIC */
#define play_music()
#define music_stop()

/* ---- screen.h redraw/scoreline flags ---- */
#define REDRAW_INITIALISE 15
#define REDRAW_EVERYTHING 7
#define REDRAW_INTERMEDIATE 3
#define REDRAW_ANIMATED 1
#define SCORELINE_ICONS 1
#define SCORELINE_SCREWS 2
#define SCORELINE_KEYS 4
#define SCORELINE_BULLETS 8
#define SCORELINE_LEVEL 16
#define SCORELINE_PACK 32
#define SCORELINE_AUTHOR 64
#define FADE_SUB_INITIALISE 3
#define FADE_SUB_SHOW 1
#define FADE_SUB_KILL FADE_SUB_INITIALISE

/* ---- rcfile (capsule-completion save path; never taken on GBC) ---- */
#define RCFILE_SAVE_ON_CHANGE 1
GR_GLOBAL struct { int save_frequency; } rcfile;
GR_GLOBAL char *path_resource_file;
void save_resource_file(char *path, int arg);

/* ---- the level (only fields board.c + the GBC loader use) ---- */
GR_GLOBAL struct {
    int w;
    int h;
    int now_is_blinking;	/* >0 blinks the background white after exit opens */
    Uint32 colour;		/* level background colour (BGR not yet) */
} level;

/* ---- level packs (board.c uses 3 fields for progression) ---- */
GR_GLOBAL struct {
    int last_level;
    int level_reached;
    int level_selected;
} level_packs[1];
GR_GLOBAL int selected_pack;
GR_GLOBAL unsigned long gr_score;	/* points (screws/keys/ammo collected); shown on pause */

/* ---- scoreline / game_area / intro_screen redraw bookkeeping ---- */
GR_GLOBAL struct { int redraw; } scoreline;
GR_GLOBAL struct { int redraw; } game_area;
GR_GLOBAL struct { int redraw; } intro_screen;

/* ---- video metrics (only referenced by the disabled gcoord/set_images) ---- */
GR_GLOBAL struct { int field_size; } video;

/* ---- misc globals ---- */
GR_GLOBAL unsigned int cycle_count; /* wrapping cycle count for time stamping */
GR_GLOBAL int game_mode;
GR_GLOBAL int sound;

#include "board.h"

/* ---- function prototypes the logic relies on (provided by the GBC shim) ---- */
int  my_rand(void);
void my_srand(unsigned int seed);
int  rand(void);
int  abs(int v);
void play_sound(int event, int vol);
int  show_game_area(void);
int  show_game_area_fade(int subfunction, int type);
int  level_init(void);
int  load_level_data(int level_number);
void manage_game_on_input(int actionid);

#endif
