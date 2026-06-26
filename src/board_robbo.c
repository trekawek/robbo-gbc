/* GBC: move_robbo + shoot_robbo moved out of board.c (HOME) into a switchable
   bank to keep the non-banked _CODE under the 0x4000 limit (these two are the
   largest functions in the engine).  They run only on player input (a few times
   per second), so the __banked trampoline cost is negligible.  They call HOME
   helpers (create_object/clear_field/can_move/move_object/play_sound) and the
   __banked find_teleport - all handled transparently by the trampoline. */
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
#include "board_internal.h"

void
move_robbo(int x, int y) __banked
{
    int             x_tmp,
                    y_tmp,
                    x2_tmp,
                    y2_tmp;
    int             i,
                    j,
                    k;
    int             dir_tmp;
    struct Coords   coords;

    if(robbo.blocked || robbo.moved>0) {
	
	return;
	}
//    if (robbo.moved > 0)
//	/* robbo cannot move yet */
//	return;

    x_tmp = robbo.x + x;
    y_tmp = robbo.y + y;

    if ((x == 1) && (y == 0))
	robbo.direction = 0;
    else if ((x == 0) && (y == 1))
	robbo.direction = 2;
    else if ((x == -1) && (y == 0))
	robbo.direction = 4;
    else if ((x == 0) && (y == -1))
	robbo.direction = 6;

    if (x_tmp < 0 || x_tmp >= level.w || y_tmp < 0 || y_tmp >= level.h)
	return;

    redraw_field(robbo.x, robbo.y);	/* Redraw the source field */

    switch (board[x_tmp][y_tmp].type) {
    case STOP:
	clear_field(x_tmp, y_tmp);
	break;
    case RADIOACTIVE_FIELD:
	clear_field(x_tmp, y_tmp);
	kill_robbo();
	break;

    case EMPTY_FIELD:
    case ROBBO:		/* Robbo can move there */
	redraw_field(x_tmp, y_tmp);	/* Redraw destination field */
	break;
    case SCREW:

	if (robbo.screws > 0) {
	    robbo.screws--;
	}

	if (robbo.screws == 0) {	/* play sound for open exit */
	    play_sound(SFX_EXIT_OPEN, SND_NORM);
	    open_exit();
	} else
	    play_sound(SFX_SCREW, SND_NORM);	/* play sound for collected screw */

	scoreline.redraw |= SCORELINE_SCREWS;
	clear_field(x_tmp, y_tmp);
	break;
    case BULLET:
	play_sound(SFX_BULLET, SND_NORM);	/* play sound for collected ammo */
	robbo.bullets += 9;
	scoreline.redraw |= SCORELINE_BULLETS;
	clear_field(x_tmp, y_tmp);
	break;
    case KEY:
	play_sound(SFX_KEY, SND_NORM);	/* play sound for collected key */
	robbo.keys++;
	scoreline.redraw |= SCORELINE_KEYS;
	clear_field(x_tmp, y_tmp);
	break;
    case DOOR:			/* Robbo cannot move */
	if (robbo.keys > 0) {	/* should open the door first */
	    robbo.keys--;
	    scoreline.redraw |= SCORELINE_KEYS;
	    clear_field(x_tmp, y_tmp);
	    robbo.moved = DELAY_ROBBO;
	    play_sound(SFX_DOOR, SND_NORM);	/* play sound for door open */
	}
	return;
    case TELEPORT:
	if (board[x_tmp][y_tmp].teleportnumber == 0)
	    break;
	play_sound(SFX_TELEPORT, SND_NORM);	/* teleport sound */
	i = 0;
	j = board[x_tmp][y_tmp].teleportnumber2;

	while (i == 0) {
	    j++;

	    if (((find_teleport
		  (&coords, board[x_tmp][y_tmp].teleportnumber, j)) == 0)
		&& j != board[x_tmp][y_tmp].teleportnumber2) {	/* teleport not found */
		if (j > MAX_TELEPORT_IDS)
		    j = -1;
		continue;
	    }

	    dir_tmp = (robbo.direction / 2);	/* neurocyp * new teleport logic now it should be the same as original robbo logic */
	    for (k = 0; k < 4; k++) {	/* first time, the direction, where robbo tries to go is checked */
		if (can_move(coords, dir_tmp)) {
		    create_object(robbo.x, robbo.y, TELEPORTING);	/* Create src teleporting animation */
		    update_coords(&coords, dir_tmp);
		    x_tmp = coords.x;
		    y_tmp = coords.y;
		    robbo.direction = (dir_tmp * 2);
		    create_object(x_tmp, y_tmp, TELEPORTING);	/* Create dest teleporting animation */
		    robbo.moved = DELAY_TELEPORTING * 5;	/* 5 frames of teleporting animation */
		    viewport.cycles_to_dest = robbo.moved;
		    robbo.teleporting = TRUE;
		    i = 1;
		    break;
		}
		dir_tmp = dir_tmp ^ (((k + 1) % 2) + 2);
	    }
	    if (j == board[x_tmp][y_tmp].teleportnumber2 && i == 0) {	/* protect from freezing the game */
		create_object(robbo.x, robbo.y, TELEPORTING);	/* Create src/dest teleporting animation */
		robbo.moved = DELAY_TELEPORTING * 5;	/* 5 frames of teleporting animation */
		viewport.cycles_to_dest = robbo.moved;
		robbo.teleporting = TRUE;
		i = 1;
		return;
	    }
	}
	break;
    case CAPSULE:
	if (robbo.exitopened) {
	    play_sound(SFX_CAPSULE, SND_NORM);
	    /* Signal the advance only; the glue loop (HOME) does the actual
	       level_init() reload.  level_init() -> load_level_data() switches the
	       ROM bank to read the level data, which CORRUPTS this banked module
	       if run from here (move_robbo's own bank gets unmapped on return) -
	       that was the capsule-completion hang (e.g. finishing level 12). */
	    if (level_packs[selected_pack].level_selected >=
		level_packs[selected_pack].last_level)
		game_mode = END_SCREEN;
	    else
		level_packs[selected_pack].level_selected++;
	    return;
	}
    case BOX:			/* Moveable objects */
    case PUSH_BOX:
    case BOMB:
    case BOMB2:
    case QUESTIONMARK:
    case GUN:
	if ((board[x_tmp][y_tmp].type == GUN) && (board[x_tmp][y_tmp].movable == 0))
	    return;
	y2_tmp = robbo.y + (2 * y);
	x2_tmp = robbo.x + (2 * x);
	if (x2_tmp < 0 || x2_tmp >= level.w || y2_tmp < 0 || y2_tmp >= level.h)
	    return;

	coords.x = x2_tmp;
	coords.y = y2_tmp;

	if (board[x2_tmp][y2_tmp].type == EMPTY_FIELD) {
	    play_sound(SFX_BOX, SND_NORM);
	    if (board[x_tmp][y_tmp].type == PUSH_BOX) {
		board[x_tmp][y_tmp].state = 1;	/* box is pushed */
		board[x_tmp][y_tmp].direction = (robbo.direction / 2);
	    }
	    move_object(x_tmp, y_tmp, coords);
	    if (board[coords.x][coords.y].type != GUN)
		SET_MOVED(coords.x, coords.y, DELAY_PUSHBOX);
	} else {
	    robbo.moved = DELAY_ROBBO;
	    return;
	}
	break;
    default:			/* every else objects */
	return;
    }

    robbo.x = x_tmp;
    robbo.y = y_tmp;

    if (robbo.moved == 0) {
	robbo.moved = DELAY_ROBBO;	/* delay robbo */
	if(!robbo.blocked)
    		play_sound(SFX_ROBBO, SND_NORM);
}

}

void
shoot_robbo(int x, int y) __banked
{
    int             x_tmp,
                    y_tmp;

    if (robbo.shooted > 0)
	return;

    /*
     * Is Robbo currently being pulled by a magnet or teleporting? 
     */
    if (robbo.blocked || robbo.teleporting)
	return;			/* Matches original Robbo */

    x_tmp = robbo.x + x;
    y_tmp = robbo.y + y;

    if ((x == 1) && (y == 0))
	robbo.direction = 0;
    else if ((x == 0) && (y == 1))
	robbo.direction = 2;
    else if ((x == -1) && (y == 0))
	robbo.direction = 4;
    else if ((x == 0) && (y == -1))
	robbo.direction = 6;

    redraw_field(robbo.x, robbo.y);

    if (robbo.bullets == 0)
	return;
    if (x_tmp < 0 || x_tmp >= level.w || y_tmp < 0 || y_tmp >= level.h) {
	robbo.bullets--;
	robbo.shooted = DELAY_LASER;
	return;
    }

    switch (board[x_tmp][y_tmp].destroyable) {
    case 1:			/* objects can be destroyed */
	SET_BLOWED(x_tmp, y_tmp, 1);
	play_sound(SFX_SHOOT, SND_QUIET);

	if (board[x_tmp][y_tmp].type != BOMB) {
	    // printf("%d\n", board[x_tmp][y_tmp].type);
	    play_sound(SFX_KILL, SND_NORM);
	}
	robbo.shooted = DELAY_LASER;
	robbo.bullets--;
	scoreline.redraw |= SCORELINE_BULLETS;
	return;
    case 0:
	play_sound(SFX_SHOOT, SND_NORM);

	robbo.shooted = DELAY_LASER;
	robbo.bullets--;
	scoreline.redraw |= SCORELINE_BULLETS;
	break;
    }

    switch (board[x_tmp][y_tmp].type) {
    case EMPTY_FIELD:
	if (robbo.direction == 0 || robbo.direction == 4)
	    create_object(x_tmp, y_tmp, LASER_L);
	else
	    create_object(x_tmp, y_tmp, LASER_D);

	SET_MOVED(x_tmp, y_tmp, DELAY_LASER);
	board[x_tmp][y_tmp].direction = robbo.direction / 2;
	break;
    }
}
