#ifndef GAME_H
#define GAME_H
#include "level.h"

typedef struct {
    unsigned char robbo_x, robbo_y;
    unsigned char facing;      /* 0=up 1=down 2=left 3=right */
    unsigned char moving;      /* set the tick robbo walked (for facing animation) */
    unsigned int  screws;      /* remaining to collect */
    unsigned char ammo;
    unsigned char keys;
    unsigned char lives;
    unsigned char level;       /* current level index (CNUM) */
    unsigned char exit_open;   /* set when screws == 0 */
    unsigned char dead;        /* robbo died this life */
    unsigned char won;         /* reached the open exit */
    unsigned long score;
} GameState;

extern GameState gs;

/* direction deltas */
extern const signed char DX[4], DY[4];

void game_start_level(unsigned char idx);
void game_run(void);

/* helpers shared with player/objects */
void mark_dirty(unsigned char x, unsigned char y);

#endif
