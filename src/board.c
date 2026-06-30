/*  GNU Robbo
 *  Copyright (C) 2002-2010 The GNU Robbo Team (see AUTHORS).
 *
 *  GNU Robbo is free software - you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  GNU Robbo is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the impled warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU CC; see the file COPYING. If not, write to the
 *  Free Software Foundation, 59 Temple Place - Suite 330,
 *  Boston, MA 02111-1307, USA.
 *
 */

#include "game.h"
#include "board_internal.h"

/* Defines */
/*
#define DEBUG_MONITOR_OBJECT_PROCESSING
#define DEBUG_INSPECT_OBJECT_CONTENTS
#define DEBUG_BEAR_LOGIC
*/

#define MAX_TELEPORT_IDS 15	/* max number of teleports of one kind at level */

/*
 * Variables 
 */


/*
 * Function prototypes 
 */
int             random_id(void) __banked;
int             update_coords(struct Coords *coords, int direction);

void            set_images(int type, int x, int y);
int             can_move(struct Coords coords, int direction);
void            move_object(int x, int y, struct Coords coords);
void            shoot_object(int x, int y, int direction);
void            check_object_if_blowed(int x, int y);
int             is_robbo_killed(void);

/* GBC perf: 1 for object types that have a case in update_game's switch (i.e.
   have per-cycle behaviour).  Cells of these types stay "active" (inlist=1);
   everything else is only active while it has a pending move/blow delay. */
const unsigned char gr_act_type[71] = {
    [BEAR] = 1, [BEAR_B] = 1, [BARRIER] = 1, [BIRD] = 1, [BUTTERFLY] = 1,
    [BLASTER] = 1, [CAPSULE] = 1, [LITTLE_BOOM] = 1, [LASER_L] = 1, [LASER_D] = 1,
    [BIG_BOOM] = 1, [RADIOACTIVE_FIELD] = 1, [TELEPORT] = 1, [TELEPORTING] = 1,
    [PUSH_BOX] = 1, [MAGNET] = 1, [GUN] = 1,
};



/***************************************************************************
 * Update Game                                                             *
 ***************************************************************************/
/*
 * Main function for the game - called every game cycle.
 * 
 * Thunor: Objects that I have 'processed' checked that required it :- [x] 
 * - BEAR/_B [x] - BARRIER [x] - BIRD [x] - BUTTERFLY [x] - BLASTER [x] -
 * LASER_L/D [x] - PUSH_BOX [x] - GUN
 * 
 */




void
update_game(void)
{
    int             x_tmp,
                    flag,
                    sflag,
                    temp_state = 0,
	temp_blowed = 0,
	temp_direction = 0;
    struct Coords   coords,
                    coords_temp,
                    dest,
                    coords_side_behind,
                    coords_behind;
    int             x,
                    y,
                    i,
                    forceforward;

    dest.x = 0;			/* to avoid warnings */
    dest.y = 0;
    play_music();
    /*
     * Firstly, update Robbo 
     */
    if (is_robbo_killed())
	kill_robbo();		/* If Robbo has been killed then mark everything to be blown-up */
    if (robbo.shooted > 0)
	robbo.shooted--;	/* Decrement shot delay */
    if (robbo.moved > 0)
	robbo.moved--;		/* Decrement movement delay */
    if (robbo.moved == 0) {
	robbo.teleporting = FALSE;
	redraw_field(robbo.x, robbo.y);
	/*
	 * Is Robbo currently being pulled by a magnet? 
	 */
	if (robbo.blocked == 1) {

	    set_coords(&coords, robbo.x, robbo.y);
	    if (can_move(coords, robbo.blocked_direction)) {
		play_sound(SFX_MAGNET, SND_NORM);	/* play magnet sound */
		switch (robbo.blocked_direction) {
		case 0:
		    robbo.x++;
		    break;
		case 2:
		    robbo.x--;
		    break;
		case 1:
		    robbo.y++;
		    break;
		case 3:
		    robbo.y--;
		    break;
		}
		robbo.moved = DELAY_MAGNET_ATTRACT;
		redraw_field(robbo.x, robbo.y);
	    } else {
		kill_robbo();
		return;		/* Thunor: this was already here; I didn't add this, but I did add some lower down */
	    }
	}
    } else if (robbo.moved == DELAY_ROBBO_ANIMATE) {
	robbo.state = !robbo.state;	/* Toggle Robbo's image */
	redraw_field(robbo.x, robbo.y);
    }

    /*
     * Now iterate through every board location updating objects 
     */
    /* GBC perf: walk each row with an incremental cell pointer instead of
       board[x][y] (the GB has no hardware multiply, so the per-access
       x*MAX_H+y stride was the dominant cost).  c == &board[x][y]; advancing x
       is just a constant pointer add. */
#ifndef PROF_SKIP_SCAN
    for (y = 0; y < level.h; y++) {	/* Thunor: I have swapped these around to process by rows */
	struct object *c;
	unsigned char rc;
#ifndef GR_NO_ROWSKIP
	if (!gr_row_active[y])		/* GBC perf: skip rows with no active cell */
	    continue;
#endif
	c = &board[0][y];
	rc = 0;
	for (x = 0; x < level.w; x++, c += MAX_H) {
	    if (!c->inlist)             /* GBC perf: skip inert cells (one bit test) */
		continue;

	    /*
	     * Thunor: I've added this check to filter out already
	     * processed objects. Additionally the object logic updates
	     * other objects and I have modified the code to mark those as
	     * processed too. The reason I am doing this is because objects
	     * can get moved ahead of the x/y loop resulting in them having
	     * at least their shooted/rotated/moved properties decremented
	     * more than once for this cycle and they go out of sync
	     */
	    if (c->processed != cycle_count) {
		c->processed = cycle_count;

		/*
		 * GUN and BIRD objects can rotate and shoot so update
		 * their delays here
		 */
		if (c->type == GUN || c->type == BIRD || c->type == MAGNET) {
		    if (c->shooted > 0)
			c->shooted--;	/* Decrement shot delay */
		    if (c->rotated > 0)
			c->rotated--;	/* Decrement rotation delay */
		}

		/*
		 * Decrement the object's delay and then check if it needs
		 * processing
		 */
		if (c->moved > 0)
		    c->moved--;
		if (c->moved <= 0) {
		    check_object_if_blowed(x, y);	/* blow objects marked for blowing */

		    set_coords(&coords, x, y);

			switch (c->type) {
			case BEAR: case BEAR_B:
			    if (upd_g1(x, y)) return; break;
			case BARRIER:
			    if (upd_g2(x, y)) return; break;
			case BIRD: case BUTTERFLY: case BLASTER:
			case CAPSULE: case LITTLE_BOOM:
			    if (upd_g3(x, y)) return; break;
			case LASER_L: case LASER_D: case BIG_BOOM:
			case RADIOACTIVE_FIELD: case TELEPORT: case TELEPORTING:
			    if (upd_g4(x, y)) return; break;
			case PUSH_BOX: case MAGNET: case GUN:
			    if (upd_g5(x, y)) return; break;
			default: break;
			}
		}
			if (!gr_act_type[c->type] && c->moved == 0 && !c->blowed)
			    c->inlist = 0;
	    }			/* .processed check */
	}
	/* Recompute the row's active count from TRUTH by re-scanning all cells'
	   inlist.  An object that moved WEST activated a cell the forward scan
	   already passed; counting during the scan would miss it and overwrite
	   the SET_MOVED bump, freezing the object (e.g. west-facing bears). */
	{
	    struct object *cc = &board[0][y];
	    rc = 0;
	    for (x = 0; x < level.w; x++, cc += MAX_H)
		if (cc->inlist)
		    rc++;
	}
	gr_row_active[y] = rc;
    }
#endif /* PROF_SKIP_SCAN */
}

/************************************************************/
/********** Init questionmark objects **********************/
/************************************************************/


/******************************************************/
/*
 * Update Coords *************************************
 */
/******************************************************/
/*
 * Note that if modifying the coords results in them going off-board then
 * the coords returned are the same as passed 
 */

int
update_coords(struct Coords *coords, int direction)
{
    struct Coords   coords_temp;
    coords_temp.x = coords->x;
    coords_temp.y = coords->y;

    if (direction == 0)
	coords->x++;
    else if (direction == 1)
	coords->y++;
    else if (direction == 2)
	coords->x--;
    else
	coords->y--;

    if (coords->x < 0 || coords->y < 0 || coords->x >= level.w
	|| coords->y >= level.h) {
	set_coords(coords, coords_temp.x, coords_temp.y);
	return 1;
    }
    return 0;
}

/******************************************************/
/*
 * Set Coords ****************************************
 */
/******************************************************/

void
set_coords(struct Coords *coords, int x, int y)
{
    coords->x = x;
    coords->y = y;
}

/******************************************************/
/*
 * Coords Out Of Range *******************************
 */
/******************************************************/

int
coords_out_of_range(struct Coords coords)
{
    if (coords.x < 0 || coords.x >= level.w || coords.y < 0
	|| coords.y >= level.h)
	return 1;
    else
	return 0;
}

/*************************************************************/
/*
 * Negating object state (birds waves their wings etc.. *****
 */
/*************************************************************/

void
negate_state(int x, int y)
{
    if (x < level.w && y < level.h) {
	board[x][y].state = !board[x][y].state;
	board[x][y].redraw = TRUE;
    }
}

/******************************************************/
/*
 * Set Images ****************************************
 */
/******************************************************/
/*
 * translate coordinates in the icons file, so we do not have to know the
 * precise values, just where the icon is located 
 */
int
gcoord(int a)
{				/* this should be a macro, but if we would change the border sizes, we should include it now for tile size 16 border size is 1 and for ts=32 the border size is 2 */
    return ((video.field_size / 16) * a) + ((a - 1) * video.field_size);
}


/*
 * Neurocyp: refactored it a bit, so it could be ready to increase the
 * tile size in the future 
 */
void
set_images(int type, int x, int y)
{
    /* GBC port: icon[] sprite-coord cache removed to fit GB RAM; the GBC
       backend renders directly from type/state/direction.  Original SDL body
       preserved below under #if 0. */
    (void)type; (void)x; (void)y;
    return;
#if 0
    switch (type) {
    case EMPTY_FIELD:
	set_coords(&board[x][y].icon[0], 0, 0);
	break;
    case WALL:			/* setting coords for icon */
	set_coords(&board[x][y].icon[0], gcoord(3), gcoord(1));	/* WALL */
	set_coords(&board[x][y].icon[1], gcoord(4), gcoord(1));	/* WALL_RED */
	set_coords(&board[x][y].icon[2], gcoord(6), gcoord(3));	/* WALL_GREEN */
	set_coords(&board[x][y].icon[3], gcoord(8), gcoord(2));	/* BLACK_WALL */
	set_coords(&board[x][y].icon[4], gcoord(10), gcoord(2));/* FAT_WALL */
	set_coords(&board[x][y].icon[5], gcoord(9), gcoord(6));	/* ROUND_WALL */
	set_coords(&board[x][y].icon[6], gcoord(10), gcoord(6));/* BOULDER_WALL */
	set_coords(&board[x][y].icon[7], gcoord(11), gcoord(1));/* SQUARE_WALL */
	set_coords(&board[x][y].icon[8], gcoord(11), gcoord(2));/* LATTICE_WALL */
	break;
    case SCREW:
	set_coords(&board[x][y].icon[0], gcoord(5), gcoord(1));
	break;
    case BULLET:
	set_coords(&board[x][y].icon[0], gcoord(6), gcoord(1));
	break;
    case BOX:
	set_coords(&board[x][y].icon[0], gcoord(7), gcoord(1));
	break;
    case PUSH_BOX:
	set_coords(&board[x][y].icon[0], gcoord(7), gcoord(7));
	set_coords(&board[x][y].icon[1], gcoord(9), gcoord(2));	/* pushed box changes state */
	break;
    case KEY:
	set_coords(&board[x][y].icon[0], gcoord(8), gcoord(1));
	break;
   case BOMB2:
	set_coords(&board[x][y].icon[0], gcoord(8), gcoord(7));
	break;
    case BOMB:
	set_coords(&board[x][y].icon[0], gcoord(9), gcoord(1));
	break;
    case DOOR:
	set_coords(&board[x][y].icon[0], gcoord(10), gcoord(1));
	break;
    case QUESTIONMARK:
   	set_coords(&board[x][y].icon[0], gcoord(1), gcoord(2));
	break;

    case CAPSULE:
	set_coords(&board[x][y].icon[0], gcoord(6), gcoord(2));
	set_coords(&board[x][y].icon[1], gcoord(7), gcoord(2));
	break;
    case BEAR:
	set_coords(&board[x][y].icon[0], gcoord(2), gcoord(2));
	set_coords(&board[x][y].icon[1], gcoord(3), gcoord(2));
	break;
    case BIRD:
	set_coords(&board[x][y].icon[0], gcoord(4), gcoord(2));
	set_coords(&board[x][y].icon[1], gcoord(5), gcoord(2));
	break;
    case LITTLE_BOOM:
	set_coords(&board[x][y].icon[0], gcoord(2), gcoord(3));
	set_coords(&board[x][y].icon[1], gcoord(3), gcoord(3));
	set_coords(&board[x][y].icon[2], gcoord(4), gcoord(3));
	set_coords(&board[x][y].icon[3], gcoord(5), gcoord(3));
	break;
    case GROUND:
	set_coords(&board[x][y].icon[0], gcoord(6), gcoord(7));
	break;
    case BEAR_B:
	set_coords(&board[x][y].icon[0], gcoord(7), gcoord(3));
	set_coords(&board[x][y].icon[1], gcoord(8), gcoord(3));
	break;
    case BUTTERFLY:
	set_coords(&board[x][y].icon[0], gcoord(9), gcoord(3));
	set_coords(&board[x][y].icon[1], gcoord(10), gcoord(3));
	break;
    case LASER_L:
	set_coords(&board[x][y].icon[0], gcoord(1), gcoord(4));
	set_coords(&board[x][y].icon[1], gcoord(2), gcoord(4));
	set_coords(&board[x][y].icon[2], gcoord(1), gcoord(3));	/* Thunor: Added for solid lasers */
	set_coords(&board[x][y].icon[3], gcoord(6), gcoord(8));
	break;
    case LASER_D:
	set_coords(&board[x][y].icon[0], gcoord(3), gcoord(4));
	set_coords(&board[x][y].icon[1], gcoord(4), gcoord(4));
	set_coords(&board[x][y].icon[2], gcoord(6), gcoord(4));	/* Thunor: Added for solid lasers */
	set_coords(&board[x][y].icon[3], gcoord(7), gcoord(8));	/* Thunor: Added for solid lasers */

	break;
    case TELEPORT:
	set_coords(&board[x][y].icon[0], gcoord(1), gcoord(5));
	set_coords(&board[x][y].icon[1], gcoord(2), gcoord(5));
	break;
    case TELEPORTING:		/* Thunor: Added for dedicated teleport animation */
	set_coords(&board[x][y].icon[0], gcoord(11), gcoord(4));
	set_coords(&board[x][y].icon[1], gcoord(11), gcoord(5));
	set_coords(&board[x][y].icon[2], gcoord(11), gcoord(6));
	set_coords(&board[x][y].icon[3], gcoord(11), gcoord(7));
	set_coords(&board[x][y].icon[4], gcoord(11), gcoord(8));
	break;
    case BIG_BOOM:
	set_coords(&board[x][y].icon[0], gcoord(1), gcoord(8));	/* Neurocyp: Added to distinguish booms from blasters */
	set_coords(&board[x][y].icon[1], gcoord(2), gcoord(8));
	set_coords(&board[x][y].icon[2], gcoord(3), gcoord(8));
	set_coords(&board[x][y].icon[3], gcoord(4), gcoord(8));
	set_coords(&board[x][y].icon[4], gcoord(5), gcoord(8));
	break;

    case GUN:
	set_coords(&board[x][y].icon[0], gcoord(6), gcoord(5));
	set_coords(&board[x][y].icon[1], gcoord(7), gcoord(5));
	set_coords(&board[x][y].icon[2], gcoord(8), gcoord(5));
	set_coords(&board[x][y].icon[3], gcoord(9), gcoord(5));
	set_coords(&board[x][y].icon[4], gcoord(9), gcoord(8));
	set_coords(&board[x][y].icon[5], gcoord(10), gcoord(7));
	set_coords(&board[x][y].icon[6], gcoord(10), gcoord(8));
	set_coords(&board[x][y].icon[7], gcoord(9), gcoord(7));
	break;
    case MAGNET:
	set_coords(&board[x][y].icon[0], gcoord(1), gcoord(1));
	set_coords(&board[x][y].icon[1], gcoord(1), gcoord(7));
	set_coords(&board[x][y].icon[2], gcoord(2), gcoord(1));
	set_coords(&board[x][y].icon[3], gcoord(2), gcoord(7));
	break;
    case BLASTER:
	set_coords(&board[x][y].icon[0], gcoord(3), gcoord(5));
	set_coords(&board[x][y].icon[1], gcoord(4), gcoord(5));
	set_coords(&board[x][y].icon[2], gcoord(5), gcoord(5));
	set_coords(&board[x][y].icon[3], gcoord(4), gcoord(5));
	set_coords(&board[x][y].icon[4], gcoord(3), gcoord(5));
	break;
    case BARRIER:
	set_coords(&board[x][y].icon[0], gcoord(10), gcoord(4));
	set_coords(&board[x][y].icon[1], gcoord(10), gcoord(5));
	break;
    case RADIOACTIVE_FIELD:
	set_coords(&board[x][y].icon[0], gcoord(3), gcoord(7));
	set_coords(&board[x][y].icon[1], gcoord(4), gcoord(7));

	break;
    case STOP:
	set_coords(&board[x][y].icon[0], gcoord(5), gcoord(7));
	break;

    }
#endif

}

/************************************************************/
/*
 * This function checks if object can move on the field ****
 */
/*
 * described by direction and coordinates, if not returns 0 
 */
/************************************************************/

int
can_move(struct Coords coords, int direction)
{
    if (update_coords(&coords, direction) ||
	(board[coords.x][coords.y].type != EMPTY_FIELD) ||
	((robbo.x == coords.x) && (robbo.y == coords.y)) ||
	board[coords.x][coords.y].moved > 0)
	return 0;
    else
	return 1;
}

/**************************************************/
/*
 * Function moves object deleting object data ****
 */
/*
 * from one field and moves them to another ******
 */
/**************************************************/

void
move_object(int x, int y, struct Coords coords)
{
    int             x1,
                    y1;

    if ((x == coords.x) && (y == coords.y))	/* move to the same place */
	return;

    x1 = coords.x;
    y1 = coords.y;

    board[x1][y1].type = board[x][y].type;
    board[x1][y1].state = board[x][y].state;
    board[x1][y1].direction = board[x][y].direction;
    board[x1][y1].destroyable = board[x][y].destroyable;
    board[x1][y1].blowable = board[x][y].blowable;
    board[x1][y1].killing = board[x][y].killing;
    SET_MOVED(x1, y1, board[x][y].moved);
    SET_BLOWED(x1, y1, board[x][y].blowed);
    board[x1][y1].shooted = board[x][y].shooted;
    board[x1][y1].rotated = board[x][y].rotated;
    board[x1][y1].solidlaser = board[x][y].solidlaser;
    board[x1][y1].rotable = board[x][y].rotable;
    board[x1][y1].randomrotated = board[x][y].randomrotated;
    /*
     * NOTE: teleportnumber is not being moved
     */
    /*
     * NOTE: teleportnumber2 is not being moved
     */
    board[x1][y1].id_questionmark = board[x][y].id_questionmark;
    board[x1][y1].direction2 = board[x][y].direction2;
    board[x1][y1].movable = board[x][y].movable;
    /*
     * NOTE: returnlaser is not being moved
     */
    board[x1][y1].shooting = board[x][y].shooting;
    board[x1][y1].processed = board[x][y].processed;
    board[x1][y1].redraw = TRUE;

    set_images(board[x1][y1].type, x1, y1);
    clear_field(x, y);

}

/*************************************************************/
/******* If objects shoots... ********************************/
/*************************************************************/

void
shoot_object(int x, int y, int direction)
{
    struct Coords   coords;

    if (board[x][y].shooted > 0)
	return;

    set_coords(&coords, x, y);

    if (update_coords(&coords, direction)) {
	board[coords.x][coords.y].shooted = DELAY_LASER;
	return;
    }

    if ((coords.x == robbo.x) && (coords.y == robbo.y)) {
	kill_robbo();
	return;			/* This prevents more lasers being created after Robbo dies */
    }

    switch (board[coords.x][coords.y].destroyable) {
    case 1:
	if ((board[coords.x][coords.y].type != BOMB) && (board[coords.x][coords.y].type != BOMB2)){
	    if (in_viewport(x, y))
		play_sound(SFX_KILL, SND_NORM);
	    else
		play_sound(SFX_KILL, SND_QUIET);
	}
	SET_BLOWED(coords.x, coords.y, 1);
	board[coords.x][coords.y].shooted = DELAY_LASER;
	if (board[x][y].solidlaser == 1) {
	    SET_MOVED(x, y, DELAY_LASER);
	}
	return;
    default:
	break;
    }
    switch (board[coords.x][coords.y].type) {
    case EMPTY_FIELD:
	if (direction == 0 || direction == 2) {
	    clear_field(coords.x, coords.y);
	    create_object(coords.x, coords.y, LASER_L);
	} else {
	    clear_field(coords.x, coords.y);
	    create_object(coords.x, coords.y, LASER_D);
	}

	board[coords.x][coords.y].direction = direction;
	board[coords.x][coords.y].solidlaser = board[x][y].solidlaser;

	board[x][y].shooted = DELAY_LASER;
	SET_MOVED(coords.x, coords.y, DELAY_LASER);
	/*
	 * Thunor: I decided to use different animation frame(s) for the
	 * solid laser fire to make the graphics more interesting 
	 */
	if (board[coords.x][coords.y].solidlaser == 1)
		board[coords.x][coords.y].state=(board[x][y].state==3)?board[x][y].state:(rand()%2)+2;	    

	break;
    case LASER_D:
    case LASER_L:
	break;
    }
}

/****************************************************************/
/*
 * Function marks all objects blowed around the bomb ***********
 */
/****************************************************************/

void
check_object_if_blowed(int x, int y)
{
    if (board[x][y].blowed && board[x][y].type != BIG_BOOM) {
	if (board[x][y].type == BOMB)
	    blow_bomb(x, y);
	if (board[x][y].type== BOMB2)
	   blow_bomb2(x, y);
	create_object(x, y, BIG_BOOM);
	SET_MOVED(x, y, DELAY_BIGBOOM);
    }
}

/**********************************************************/
/** If Robbo collects the last screw exit opens ***********/
/**********************************************************/


/*************************************************************/
/**** Function returning coords of finding teleport **********/
/**** Updates coords of found teleport of 0 if not found *****/
/*************************************************************/


/***************************************************************************
 * Robbo initialisation                                                    *
 ***************************************************************************/
/*
 * Clear everything 
 */


/********************************************************************/
/*** Is Robbo Killed ************************************************/
/********************************************************************/

int
is_robbo_killed(void)
{
    int             retval = FALSE;

    if (!robbo.teleporting) {
	if ((robbo.x < level.w - 1 && board[robbo.x + 1][robbo.y].killing == 1) ||	/* Robbo's right */
	    (robbo.x > 0 && board[robbo.x - 1][robbo.y].killing == 1) ||	/* Robbo's left */
	    (robbo.y < level.h - 1 && board[robbo.x][robbo.y + 1].killing == 1) ||	/* Robbo's bottom */
	    (robbo.y > 0 && board[robbo.x][robbo.y - 1].killing == 1)) {	/* Robbo's top */
	    retval = TRUE;
	}
    }

    return retval;
}

/*************************************************/
/*** Robbo doesn't like this function :) *********/
/*************************************************/
/*
 * Everything except EMPTY_FIELDs and WALLs is blown-up here. Once this
 * has been executed the once, repeated calls to this function do nothing. 
 */


/***************************************************************************
 * Viewport Needs Redrawing                                                *
 ***************************************************************************/
/*
 * This function marks every field for redrawing/refreshing in
 * show_game_area() 
 */

void
viewport_needs_redrawing(void)
{
	int x, y;

	for (y = viewport.y; y < viewport.y + viewport.h; y++) {
		for (x = viewport.x; x < viewport.x + viewport.w; x++) {
			redraw_field(x, y);
		}
	}
}

/***************************************************************************
 * Redraw Field                                                            *
 ***************************************************************************/
/*
 * Marks an object for redrawing/refreshing in show_game_area() 
 */

void
redraw_field(int x, int y)
{
    if (x >= 0 && y >= 0 && x < level.w && y < level.h)
	board[x][y].redraw = TRUE;
}

/***************************************************************************
 * Field clearing                                                          *
 ***************************************************************************/
/*
 * This creates a blowable EMPTY_FIELD. This function clears
 * id_questionmark whereas create_object() doesn't 
 */

void
clear_field(int x, int y)
{
    create_object(x, y, EMPTY_FIELD);
    board[x][y].id_questionmark = 0;
}

/***************************************************************************
 * Create Object                                                           *
 ***************************************************************************/
/*
 * Thunor: I have found that most of the time clear_field() has been
 * called before this function, but there are times when it hasn't such as 
 * via check_object_if_blowed(). The reason for this is that the
 * id_questionmark property needs to be retained so that questionmarks can 
 * be recreated, and that is why some property clearing is also done below 
 * to compensate for this.
 * 
 * What I have decided to do seeing as this is such an important function,
 * is to clear everything as in clear_field() EXCEPT id_questionmark. 
 */

void
create_object(int x, int y, int type)
{
    int             count;

#ifdef DEBUG_INSPECT_OBJECT_CONTENTS
    if (board[x][y].icon[0].x != 0) {
	printf("*** Start %s ***\n", __func__);
	printf("type=%i\n", board[x][y].type);
	printf("state=%i\n", board[x][y].state);
	printf("direction=%i\n", board[x][y].direction);
	printf("destroyable=%i\n", board[x][y].destroyable);
	printf("blowable=%i\n", board[x][y].blowable);
	printf("killing=%i\n", board[x][y].killing);
	printf("moved=%i\n", board[x][y].moved);
	printf("blowed=%i\n", board[x][y].blowed);
	printf("shooted=%i\n", board[x][y].shooted);
	printf("rotated=%i\n", board[x][y].rotated);
	printf("solidlaser=%i\n", board[x][y].solidlaser);
	printf("rotable=%i\n", board[x][y].rotable);
	printf("randomrotated=%i\n", board[x][y].randomrotated);
	printf("teleportnumber=%i\n", board[x][y].teleportnumber);
	printf("teleportnumber2=%i\n", board[x][y].teleportnumber2);
	printf("id_questionmark=%i\n", board[x][y].id_questionmark);
	printf("direction2=%i\n", board[x][y].direction2);
	printf("movable=%i\n", board[x][y].movable);
	printf("returnlaser=%i\n", board[x][y].returnlaser);
	printf("shooting=%i\n", board[x][y].shooting);
	printf("processed=%i\n", board[x][y].processed);
	printf("icon[0].x=%i icon[0].y=%i\n", board[x][y].icon[0].x,
	       board[x][y].icon[0].y);
	printf("icon[1].x=%i icon[1].y=%i\n", board[x][y].icon[1].x,
	       board[x][y].icon[1].y);
	printf("icon[2].x=%i icon[2].y=%i\n", board[x][y].icon[2].x,
	       board[x][y].icon[2].y);
	printf("icon[3].x=%i icon[3].y=%i\n", board[x][y].icon[3].x,
	       board[x][y].icon[3].y);
	printf("icon[4].x=%i icon[4].y=%i\n", board[x][y].icon[4].x,
	       board[x][y].icon[4].y);
	printf("icon[5].x=%i icon[5].y=%i\n", board[x][y].icon[5].x,
	       board[x][y].icon[5].y);
	printf("icon[6].x=%i icon[6].y=%i\n", board[x][y].icon[6].x,
	       board[x][y].icon[6].y);
	printf("icon[7].x=%i icon[7].y=%i\n", board[x][y].icon[7].x,
	       board[x][y].icon[7].y);
	printf("icon[8].x=%i icon[8].y=%i\n", board[x][y].icon[8].x,
	       board[x][y].icon[8].y);
	printf("*** Stop %s ***\n", __func__);
    }
#endif

    board[x][y].type = type;
    board[x][y].state = 0;
    board[x][y].direction = 0;
    board[x][y].destroyable = 0;
    board[x][y].blowable = 0;
    board[x][y].killing = 0;
    board[x][y].moved = 0;
    board[x][y].blowed = 0;
    board[x][y].inlist = 0;	/* GBC perf: re-seeded below for active types */
    board[x][y].shooted = 0;
    board[x][y].rotated = 0;
    board[x][y].solidlaser = 0;
    board[x][y].rotable = 0;
    board[x][y].randomrotated = 0;
    board[x][y].teleportnumber = 0;
    board[x][y].teleportnumber2 = 0;
    /*
     * NOTE id_questionmark's value is retained 
     */
    board[x][y].direction2 = 0;
    board[x][y].movable = 0;
    board[x][y].returnlaser = 0;
    board[x][y].shooting = 0;
    board[x][y].processed = cycle_count;
    board[x][y].redraw = TRUE;
    (void)count;   /* GBC port: icon[] removed; clear loop below disabled */
#if 0
    for (count = 0; count < MAX_ICONS; count++) {
	board[x][y].icon[count].x = 0;
	board[x][y].icon[count].y = 0;
    }
#endif

    switch (type) {
    case ROBBO:
	robbo.x = x;
	robbo.y = y;
	robbo.state = 0;
	board[x][y].type = EMPTY_FIELD;	/* Robbo isn't a board location, so set it to EMPTY_FIELD */
	board[x][y].id_questionmark = 0;	/* May as well clear this as it's not required to be kept */
	return;			/* QUIT NOW */
    case BEAR:
    case BEAR_B:
    case BIRD:
    case BUTTERFLY:
	board[x][y].killing = 1;	/* Via their closeness */
    case BULLET:		/* The breaks have been ommitted for a reason */
    case BOMB:
	case BOMB2:
    case QUESTIONMARK:
    case GROUND:
    case BARRIER:
	board[x][y].destroyable = 1;	/* Via a shot or push box */
    case SCREW:
    case PUSH_BOX:
    case BOX:
    case KEY:
    case DOOR:
    case LASER_L:
    case LASER_D:
    case TELEPORT:
    case GUN:
    case EMPTY_FIELD:
	board[x][y].blowable = 1;	/* Via a bomb */
	break;
    case STOP:
    case RADIOACTIVE_FIELD:		// it probably would have its own logic
	board[x][y].blowable = 1;
	board[x][y].destroyable = 0;
	break;

    }

    set_images(board[x][y].type, x, y);

    /* GBC perf: seed the active set - a freshly created object with per-cycle
       behaviour must be visited by update_game.  Inert cells (walls/empty/
       items) start out of the list and are only added when SET_MOVED/SET_BLOWED
       gives them a pending action. */
    if (gr_act_type[board[x][y].type]) {
	board[x][y].inlist = 1;
	gr_row_active[y]++;		/* wake this row (recompute corrects any overcount) */
    }

}

/***************************************************************************
 * Clear Entire Board                                                      *
 ***************************************************************************/
/*
 * Clear everything 
 */


/*
 * this will return zero if x/y is in viewport and non zero otherwise 
 */
int
in_viewport(int x, int y)
{
	if (x - viewport.x <= viewport.w && x - viewport.x >= 0 && 
		y - viewport.y <= viewport.h && y - viewport.y >= 0) {
		return 1;
	}
	return 0;
}


/*********************************************************/
/** Robbo shoots - x, y means coords of Robbo ************/
/*********************************************************/


void
kill_robbo(void)
{
    int             x,
                    y;
    if (robbo.alive == 0)
	return;
    robbo.alive = 0;
    robbo.blocked = 0;
    play_sound(SFX_BOMB, SND_NORM);
    create_object(robbo.x, robbo.y, BIG_BOOM);
    SET_MOVED(robbo.x, robbo.y, DELAY_BIGBOOM);

    for (x = 0; x < level.w; x++)
	for (y = 0; y < level.h; y++)
	    switch (board[x][y].type) {
	    case EMPTY_FIELD:
	    case WALL:
		break;
	    default:
		SET_MOVED(x, y, DELAY_BIGBOOM);
		SET_BLOWED(x, y, 1);
	    }

    viewport_needs_redrawing();

    restart_timeout = DELAY_RESTART;

}

/* GBC: cold level-setup helpers kept in HOME (banking them corrupted boot). */
void
open_exit(void)
{
    robbo.exitopened = 1;
    level.now_is_blinking = DELAY_BLINKSCREEN;
    viewport_needs_redrawing();
}

void
init_robbo(void)
{
    robbo.x = 0;
    robbo.y = 0;
    robbo.alive = 1;
    robbo.state = 0;
    robbo.direction = 0;
    robbo.screws = 0;
    robbo.keys = 0;
    robbo.bullets = 0;
    robbo.moved = DELAY_ROBBO;
    robbo.shooted = 0;
    robbo.exitopened = 0;
    robbo.blocked = 0;
    robbo.blocked_direction = 0;
    robbo.teleporting = 0;
}

/* (Re)activate a cell and set its move/blow delay.  Out-of-line (called from the
   banked modules) so the many SET_MOVED/SET_BLOWED sites stay compact.  The
   inlist 0->1 transition bumps the row's active count; update_game recomputes
   the exact count when it scans the row, so this only needs to never undercount. */
void gr_set_moved(int x, int y, int v) {
    board[x][y].moved = (unsigned char)v;
    if (!board[x][y].inlist) { board[x][y].inlist = 1; gr_row_active[y]++; }
}
void gr_set_blowed(int x, int y, int v) {
    board[x][y].blowed = (unsigned char)v;
    if (!board[x][y].inlist) { board[x][y].inlist = 1; gr_row_active[y]++; }
}

void
clear_entire_board(void)
{
    int             xpos,
                    ypos;

    /* GBC perf: reset per-row active counts; the create_object active-seed
       (via clear_field->create_object below, then load_level_data) rebuilds
       them as objects are placed. */
    for (ypos = 0; ypos < MAX_H; ypos++)
	gr_row_active[ypos] = 0;

    /*
     * Fill the game board with EMPTY_FIELD objects
     */
    for (ypos = 0; ypos < MAX_H; ypos++) {
	for (xpos = 0; xpos < MAX_W; xpos++) {
	    clear_field(xpos, ypos);
	}
    }
}
