/* Banked half of the engine: the 5 update_game() object-switch slices.
   In a switchable ROM bank; called from update_game (HOME) via __banked.
   Their own calls to helpers in HOME are normal (HOME is always mapped). */
#pragma bank 255
#include "game.h"
#include "board_internal.h"

/* GBC: slice of update_game()'s object switch, split out so SDCC can
   compile it.  Case bodies are verbatim; bare `return;` -> `return 1;`
   (signals robbo died / stop this cycle). */
int upd_g1(int x, int y) __banked
{
    int forceforward;
    struct Coords coords, coords_side_behind, coords_behind;
    switch (board[x][y].type) {
		    case BEAR:
		    case BEAR_B:
			/*
			 * Left/right-hand rule maze traversal: this is
			 * the exact same way that the original Robbo does 
			 * it. The bears will attempt to manoeuvre around
			 * all obstacles but this takes time and they will
			 * get killed by laser fire and push boxes
			 * especially if they meet them head-on 
			 */
			set_coords(&coords, x, y);
			if (game_mode == GAME_ON) {
			    if (board[x][y].type == BEAR) {
				update_coords(&coords, (board[x][y].direction + 3) & 3);	/* Get coords of left board location */
			    } else {
				update_coords(&coords, (board[x][y].direction + 1) & 3);	/* Get coords of right board location */
			    }
			}
			forceforward = FALSE;
			/*
			 * Are we implementing sensible_bears? In the
			 * Atari Robbo, there are two conditions that
			 * place bears into irritating states that become
			 * annoying: single bears in empty space going
			 * around themselves and two bears of the same
			 * type going around one another.
			 * 
			 * |N| | = If this is a BEAR_B taking rights, it
			 * will go clockwise forever. | | | N is north.
			 * 
			 * | |N| = If these are BEARs taking lefts, they
			 * will go anti-clockwise forever. |S| | N is
			 * north, S is south.
			 * 
			 * sensible_bears detects the conditions that lead 
			 * to these problems and fixes them by forcing the 
			 * bears to go forward until they find another
			 * obstacle to navigate around 
			 */
			if (game_mechanics.sensible_bears) {
			    set_coords(&coords_side_behind, coords.x,
				       coords.y);
			    update_coords(&coords_side_behind, (board[x][y].direction + 2) & 3);	/* Get coords of side-behind board location */
			    set_coords(&coords_behind,
				       coords_side_behind.x,
				       coords_side_behind.y);
			    if (board[x][y].type == BEAR) {
				update_coords(&coords_behind, (board[x][y].direction + 1) & 3);	/* Get coords of behind board location */
			    } else {
				update_coords(&coords_behind, (board[x][y].direction + 3) & 3);	/* Get coords of behind board location */
			    }
			    if (board[coords.x][coords.y].type ==
				EMPTY_FIELD
				&& board[coords_behind.x][coords_behind.y].
				type == EMPTY_FIELD
				&& (board[coords_side_behind.x]
				    [coords_side_behind.y].type ==
				    EMPTY_FIELD
				    || board[x][y].type ==
				    board[coords_side_behind.x]
				    [coords_side_behind.y].type)) {
				forceforward = TRUE;
			    }
			}
#ifdef DEBUG_BEAR_LOGIC
			if (board[x][y].type == BEAR) {
			    printf("|%02i| @| %06i: BEAR at %03i.%03i\n",
				   board[coords.x][coords.y].type,
				   cycle_count, x, y);
			    printf("|%02i|%02i|\n",
				   board[coords_side_behind.x]
				   [coords_side_behind.y].type,
				   board[coords_behind.x][coords_behind.y].
				   type);
			} else if (board[x][y].type == BEAR_B) {
			    printf("| *|%02i| %06i: BEAR_B at %03i.%03i\n",
				   board[coords.x][coords.y].type,
				   cycle_count, x, y);
			    printf("|%02i|%02i|\n",
				   board[coords_behind.x][coords_behind.y].
				   type, board[coords_side_behind.x]
				   [coords_side_behind.y].type);
			}
#endif
			/*
			 * Check left/right and if it's clear then take it 
			 */
			if (board[coords.x][coords.y].type == EMPTY_FIELD
			    && !forceforward) {
			    /*
			     * Left/right is clear so change to that
			     * direction and move there 
			     */
			    if (board[x][y].type == BEAR) {
				board[x][y].direction = (board[x][y].direction + 3) & 3;	/* Go left */
			    } else {
				board[x][y].direction = (board[x][y].direction + 1) & 3;	/* Go right */
			    }
			    SET_MOVED(x, y, DELAY_BEAR);
			    negate_state(x, y);
			    move_object(x, y, coords);
			} else {
			    /*
			     * Left/right is not clear so check forward
			     * and if it's clear then take it 
			     */
			    set_coords(&coords, x, y);
			    update_coords(&coords, board[x][y].direction);	/* Get coords of forward board location */
			    if (board[coords.x][coords.y].type ==
				EMPTY_FIELD) {
				/*
				 * Forward is clear so move there 
				 */
				SET_MOVED(x, y, DELAY_BEAR);
				negate_state(x, y);
				move_object(x, y, coords);
			    } else {
				/*
				 * Forward is not clear so rotate
				 * rightward but don't move 
				 */
				if (board[x][y].type == BEAR) {
				    board[x][y].direction = (board[x][y].direction + 1) & 3;	/* Go forward */
				} else {
				    board[x][y].direction = (board[x][y].direction + 3) & 3;	/* Go forward */
				}
				SET_MOVED(x, y, DELAY_BEAR);
				board[x][y].processed = cycle_count;	/* Because we're not moving, we do this manually */
				negate_state(x, y);
			    }
			}
			break;

			/*********************/
			/* BARRIER logic     */
			/*********************/
    }
    return 0;
}

/* GBC: slice of update_game()'s object switch, split out so SDCC can
   compile it.  Case bodies are verbatim; bare `return;` -> `return 1;`
   (signals robbo died / stop this cycle). */
int upd_g2(int x, int y) __banked
{
    int x_tmp, flag, temp_state = 0, temp_blowed = 0, temp_direction = 0, i;
    int blo, bhi, bsolid;
    struct Coords dest;
    /* A lone wrapping barrier can reach the flag branch without a move. */
    dest.x = 0; dest.y = 0;
    switch (board[x][y].type) {
		    case BARRIER:
			/* GBC perf: a SOLID barrier run (wall-to-wall, no gaps)
			   renders identically whether or not the wave "shifts" it -
			   cell_glyph draws BARRIER state-independently and the cells
			   don't actually move - so the original move_object/
			   negate_state just re-uploads ~14 UNCHANGED tiles to VRAM
			   every pulse (the bulk of these levels' render cost).
			   Detect a solid run and skip the wave: re-arm the pulse
			   delay and kill robbo only if he's somehow on it.  GAPPED
			   fields (where the moving gap IS visible) fall through to
			   the full wave below. */
			blo = x; bhi = x;
			while (blo > 0 && board[blo - 1][y].type != WALL)
			    blo--;
			while (bhi < level.w - 1 && board[bhi + 1][y].type != WALL)
			    bhi++;
			bsolid = 1;
			for (i = blo; i <= bhi; i++)
			    if (board[i][y].type != BARRIER) {
				bsolid = 0;
				break;
			    }
			if (bsolid) {
			    if (robbo.y == y && robbo.x >= blo && robbo.x <= bhi) {
				kill_robbo();
				return 1;
			    }
			    for (i = blo; i <= bhi; i++)
				SET_MOVED(i, y, DELAY_BARRIER);  /* re-arm; no redraw */
			    break;
			}
			flag = 0;
			x_tmp = x;

			if (board[x][y].direction == 0) {	/* East -> */
			    while (x_tmp < level.w
				   && board[x_tmp][y].type != WALL)
				x_tmp++;

			    x_tmp--;

			    if (board[x_tmp][y].type == BARRIER) {
				temp_state = board[x_tmp][y].state;
				temp_blowed = board[x_tmp][y].blowed;
				temp_direction = board[x_tmp][y].direction;
				clear_field(x_tmp, y);
				negate_state(x_tmp, y);
				flag = 1;
			    }

			    while (x_tmp >= 0
				   && board[x_tmp][y].type != WALL) {
				x_tmp--;

				/*
				 * Thunor: I added "x_tmp >= 0 && " below
				 * to prevent referencing invalid array
				 * elements as x_tmp can be -1 here if the 
				 * barrier is up against the left map
				 * edge. Following this block of code,
				 * x_tmp is incremented and so this is the 
				 * most suitable solution 
				 */
				if (x_tmp >= 0
				    && board[x_tmp][y].type == BARRIER) {
				    check_object_if_blowed(x_tmp, y);
				    dest.x = x_tmp + 1;
				    dest.y = y;

				    if ((dest.x == robbo.x)
					&& (dest.y == robbo.y)) {
					kill_robbo();
					return 1;	/* Thunor: Exiting here stops new barriers from being created */
				    }

				    move_object(x_tmp, y, dest);
				    /*
				     * board[dest.x][dest.y].direction =
				     * 0; 
				     */
				    SET_MOVED(dest.x, dest.y, DELAY_BARRIER);
				    board[dest.x][dest.y].processed = cycle_count;	/* Thunor: Prevents this object from being processed again this cycle */
				    negate_state(dest.x, dest.y);
				}
			    }

			    x_tmp++;

			    if (flag == 1) {
				if ((dest.x == robbo.x)
				    && (dest.y == robbo.y)) {
				    kill_robbo();
				    return 1;	/* Thunor: Exiting here stops new barriers from being created */
				}

				if (robbo.x == x_tmp && robbo.y == y) {
				    kill_robbo();
				    return 1;
				}

				create_object(x_tmp, y, BARRIER);
				board[x_tmp][y].state = temp_state;
				SET_BLOWED(x_tmp, y, temp_blowed);
				board[x_tmp][y].direction = temp_direction;
				SET_MOVED(x_tmp, y, DELAY_BARRIER);
				negate_state(x_tmp, y);
			    }
			} else if (board[x][y].direction == 2) {	/* <- West */
			    while (x_tmp >= 0
				   && board[x_tmp][y].type != WALL)
				x_tmp--;

			    x_tmp++;

			    if (board[x_tmp][y].type == BARRIER) {
				temp_state = board[x_tmp][y].state;
				temp_blowed = board[x_tmp][y].blowed;
				temp_direction = board[x_tmp][y].direction;
				clear_field(x_tmp, y);
				negate_state(x_tmp, y);
				flag = 1;
			    }

			    while (x_tmp < level.w
				   && board[x_tmp][y].type != WALL) {
				x_tmp++;

				/*
				 * Thunor: I added "x_tmp < level.w && "
				 * below to prevent referencing invalid
				 * array elements as x_tmp can be level.w
				 * here if the barrier is up against the
				 * right map edge. Following this block of 
				 * code, x_tmp is decremented and so this
				 * is the most suitable solution 
				 */
				if (x_tmp < level.w
				    && board[x_tmp][y].type == BARRIER) {
				    check_object_if_blowed(x_tmp, y);
				    dest.x = x_tmp - 1;
				    dest.y = y;

				    if ((dest.x == robbo.x)
					&& (dest.y == robbo.y)) {
					kill_robbo();
					return 1;	/* Thunor: Exiting here stops new barriers from being created */
				    }

				    move_object(x_tmp, y, dest);
				    /*
				     * board[dest.x][dest.y].direction =
				     * 2; 
				     */
				    SET_MOVED(dest.x, dest.y, DELAY_BARRIER);
				    board[dest.x][dest.y].processed = cycle_count;	/* Thunor: Prevents this object from being processed again this cycle */
				    negate_state(dest.x, dest.y);
				}
			    }

			    x_tmp--;

			    if (flag == 1 && temp_blowed == 0) {
				if ((dest.x == robbo.x)
				    && (dest.y == robbo.y)) {
				    kill_robbo();
				    return 1;	/* Thunor: Exiting here stops new barriers from being created */
				}

				if (robbo.x == x_tmp && robbo.y == y) {
				    kill_robbo();
				    return 1;
				}

				create_object(x_tmp, y, BARRIER);
				board[x_tmp][y].state = temp_state;
				SET_MOVED(x_tmp, y, DELAY_BARRIER);
				board[x_tmp][y].direction = temp_direction;
				negate_state(x_tmp, y);
			    }
			}
			break;

						/*********************/
						/* BIRD logic        */
						/*********************/
    }
    return 0;
}

/* GBC: slice of update_game()'s object switch, split out so SDCC can
   compile it.  Case bodies are verbatim; bare `return;` -> `return 1;`
   (signals robbo died / stop this cycle). */
int upd_g3(int x, int y) __banked
{
    int temp_state;
    struct Coords coords, coords_temp;
    switch (board[x][y].type) {
		    case BIRD:
			set_coords(&coords, x, y);
			if (can_move(coords, board[x][y].direction)) {
			    update_coords(&coords, board[x][y].direction);
			    move_object(x, y, coords);

			} else
			    board[x][y].direction =
				(board[x][y].direction + 2) & 0x03;

			if (board[coords.x][coords.y].shooting) {
			    if (board[coords.x][coords.y].shooted == 0) {
				if ((my_rand() & 0x07) == 0) {	/* bird shoots */
				    shoot_object(coords.x, coords.y,
						 board[coords.x][coords.y].
						 direction2);
				    if (in_viewport(x, y))
					play_sound(SFX_BIRD, SND_NORM);
				   else play_sound(SFX_BIRD,SND_QUIET);
				} else
				    board[coords.x][coords.y].shooted =
					DELAY_LASER;
			    }
			}
			SET_MOVED(coords.x, coords.y, DELAY_BIRD);
			negate_state(coords.x, coords.y);
			break;
						/*********************/
						/* BUTTERFLY logic   */
						/*********************/
		    case BUTTERFLY:
			set_coords(&coords, x, y);
			if (can_move(coords, board[x][y].direction)) {
			    update_coords(&coords, board[x][y].direction);
			    move_object(x, y, coords);
			}

			if (!(rand() & 0x07))	/* chances for random move */
			    board[coords.x][coords.y].direction =
				rand() & 0x03;

			else if ((rand() & 0x01) == 0) {	/* if butterfly flies horizontally */
			    if (robbo.x > coords.x)
				board[coords.x][coords.y].direction = 0;
			    else if (robbo.x < coords.x)
				board[coords.x][coords.y].direction = 2;
			} else {	/* if butterfly flies vertically */
			    if (robbo.y > coords.y)
				board[coords.x][coords.y].direction = 1;
			    else if (robbo.y < coords.y)
				board[coords.x][coords.y].direction = 3;
			}

			SET_MOVED(coords.x, coords.y, DELAY_BUTTERFLY);
			negate_state(coords.x, coords.y);
			break;
						/*********************/
						/* BLASTER logic     */
						/*********************/
		    case BLASTER:
			set_coords(&coords, x, y);
			redraw_field(x, y);
			set_coords(&coords_temp, x, y);	/* * neurocyp now we will check if the gun was removed during blaster shot, if so, we have to react */
			temp_state =
			    board[coords_temp.x][coords_temp.y].state;
			while (board[coords_temp.x][coords_temp.y].type ==
			       BLASTER) {
			    temp_state =
				board[coords_temp.x][coords_temp.y].state;
			    update_coords(&coords_temp,
					  (board[coords_temp.x]
					   [coords_temp.y].direction +
					   2) % 4);
			}
			if (temp_state < 3
			    && board[coords_temp.x][coords_temp.y].type !=
			    GUN) {
			    create_object(coords.x, coords.y, LITTLE_BOOM);	/* so the gun was removed during shot? ok, clear it out */
			    SET_MOVED(coords.x, coords.y, DELAY_BLASTER);
			    break;
			}



			if (!update_coords
			    (&coords, board[coords.x][coords.y].direction)
			    && board[x][y].state == 0) {
			    if (robbo.x == coords.x && robbo.y == coords.y) {
				kill_robbo();
				return 1;	/* Thunor: Exiting here stops new blasters from being created */
			    }
			    switch (board[coords.x][coords.y].type) {
			    case DOOR:
			    case WALL:
			    case MAGNET:
			    case BOX:
			    case TELEPORT:
			    case PUSH_BOX:
			    case RADIOACTIVE_FIELD:
			    case STOP:
			    case GUN:
			    case LASER_D:
			    case LASER_L:
			    case BLASTER:
			    case SCREW:
			    case KEY:
			    case CAPSULE:
				break;
			    case BOMB:
			    case BOMB2:
				SET_BLOWED(coords.x, coords.y, DELAY_BLASTER);
				break;
			    default:
				clear_field(coords.x, coords.y);
				create_object(coords.x, coords.y, BLASTER);
				board[coords.x][coords.y].direction =
				    board[x][y].direction;
				board[coords.x][coords.y].direction2 =
				    board[x][y].direction2;
				SET_MOVED(coords.x, coords.y, DELAY_BLASTER);
			    }
			}
			if (board[x][y].state < 4) {
			    board[x][y].state++;
			    SET_MOVED(x, y, DELAY_BLASTER);
			} else {
			    clear_field(x, y);
			}
			break;
						/*********************/
						/* CAPSULE logic     */
						/*********************/
		    case CAPSULE:
			if (robbo.exitopened) {
			    negate_state(x, y);
			    SET_MOVED(x, y, DELAY_CAPSULE);
			}
			break;
						/*********************/
						/* LITTLE_BOOM logic */
						/*********************/
		    case LITTLE_BOOM:
			redraw_field(x, y);
			if (board[x][y].state < 3) {
			    board[x][y].state++;
			    SET_MOVED(x, y, DELAY_LITTLE_BOOM);
			} else
			    clear_field(x, y);
			break;
						/*********************/
						/* LASER_L/D logic   */
						/*********************/
    }
    return 0;
}

/* GBC: slice of update_game()'s object switch, split out so SDCC can
   compile it.  Case bodies are verbatim; bare `return;` -> `return 1;`
   (signals robbo died / stop this cycle). */
int upd_g4(int x, int y) __banked
{
    int temp_state, i;
    struct Coords coords, coords_temp;
    switch (board[x][y].type) {
		    case LASER_L:
		    case LASER_D:
			set_coords(&coords, x, y);
			redraw_field(x, y);
			update_coords(&coords, board[x][y].direction);
			SET_MOVED(x, y, DELAY_LASER);

			if ((coords.x == robbo.x) && (coords.y == robbo.y)
			    && !board[x][y].returnlaser) {
			    kill_robbo();
			    return 1;	/* Thunor: Exiting here stops new lasers from being created */
			}
			set_coords(&coords, x, y);
			if (board[x][y].solidlaser == 0) {
			    negate_state(x, y);

			    if (can_move(coords, board[x][y].direction)) {	/* normal shooting */
				update_coords(&coords, board[x][y].direction);
				move_object(x, y, coords);
				SET_MOVED(coords.x, coords.y, DELAY_LASER);
			    } else {	/* blow object if it's destroyable */
				if (!update_coords (&coords, board[x][y].direction) && board[coords.x][coords. y].destroyable) {
				    if ((board[coords.x][coords.y].type != BOMB) && (board[coords.x][coords.y].type != BOMB2)) {
					if (in_viewport(x, y)) {
					    play_sound(SFX_KILL, SND_NORM);
					} else { play_sound(SFX_KILL,SND_QUIET);}
					}
				    clear_field(x, y);
				    SET_BLOWED(coords.x, coords.y, 1);
				    redraw_field(coords.x, coords.y);
				} else {
				    play_sound(SFX_KNOCK, in_viewport(x, y) ? SND_NORM : SND_QUIET);
				    clear_field(x, y);	/* clear lasertrack */
				    create_object(x, y, LITTLE_BOOM);
				    SET_MOVED(x, y, DELAY_LITTLE_BOOM);
				    break;
				}
			    }
			} else {
				/* **************************** laser is solid ********************** */
			    set_coords(&coords_temp, x, y);	/* first let's check that laser starts from a gun, if not, means gun was moved, or blown */
			    while (board[coords_temp.x][coords_temp.y].type == board[x][y].type &&  coords_temp.x>=0 && coords_temp.y>=0 &&  coords_temp.x<=level.w && coords_temp.y<=level.h) {	/* move * on * laser */
				if(update_coords(&coords_temp, (board[x][y].direction + 2) & 0x03)) break;
				}
			    if (board[coords_temp.x][coords_temp.y].type != GUN) {	/* ok, now we are at the beginning of laser beam if it is not a gun, which is there, destroy laser */
				create_object(x, y, LITTLE_BOOM);
				break;
			    }
					/* neurocyp: set the state for the whole beam (begin)*/
  	    		    set_coords(&coords_temp, x, y);	
			    update_coords(&coords_temp, (board[x][y].direction+2) & 0x03);
			    if(board[coords_temp.x][coords_temp.y].type==GUN) {   /* are we at the beginning of the laser? */
				temp_state=(board[x][y].state==3)?2:3;
				set_coords(&coords_temp, x, y);	/* first let's check that laser starts from a gun, if not, means gun was moved, or blown */
			   	while ((board[coords_temp.x][coords_temp.y].type == board[x][y].type) && 
				board[coords_temp.x][coords_temp.y].direction==board[x][y].direction && 
				coords_temp.x>=0 && coords_temp.y>=0 &&  coords_temp.x<=level.w && coords_temp.y<=level.h) {	/* move * on * laser with the same direction and all */					
						redraw_field(coords_temp.x, coords_temp.y); /* we will have to redraw the whole beam, as we changed it's state*/
						board[coords_temp.x][coords_temp.y].state=temp_state;
						if(update_coords(&coords_temp, board[x][y].direction)) break; /* just in case, if we are unable to update coords then we quit the loop */
					}
				}			   	
					/* neurocyp: set the state for the whole beam (end)*/
			    if (can_move(coords, board[x][y].direction) && board[x][y].returnlaser == 0) {	/* if can shoot */
				update_coords(&coords, board[x][y].direction);
				shoot_object(x, y, board[x][y].direction);
				SET_MOVED(coords.x, coords.y, DELAY_LASER);
			    } else {
				if (board[x][y].returnlaser == 1) {
				    set_coords(&coords, x, y);	/* checking if laser is near gun */
				    update_coords(&coords, (board[x][y].direction + 2) & 0x03);
				    clear_field(x, y);

				    if (board[coords.x][coords.y].type == GUN) {
					create_object(x, y, LITTLE_BOOM);
					break;
				    }
				    if (board[coords.x][coords.y].type == LASER_D || board[coords.x][coords. y].type == LASER_L) {
					board[coords.x][coords. y].returnlaser = 1;
					SET_MOVED(coords.x, coords.y, DELAY_LASER);
					board[coords.x][coords.y].processed = cycle_count;	/* Thunor: Prevents this object from being processed again this cycle */
				    }
				} else 
				{
				    set_coords(&coords, x, y);
				    if (!update_coords (&coords, board[x][y].direction)) {
					if (board[coords.x][coords.y].type == LASER_D || board[coords.x][coords. y].type == LASER_L) {
					    if (board[coords.x] [coords.y].direction == board[x][y].direction) {
						break;
						}
					}
					if (board[coords.x] [coords.y].destroyable) {
					    SET_BLOWED(coords.x, coords. y, 1);
					    if ((board[coords.x][coords.y].type != BOMB) && (board[coords.x][coords.y].type != BOMB2)) {
						if (in_viewport(x, y))
						    play_sound(SFX_KILL, SND_NORM);
					      else
						 play_sound(SFX_KILL,SND_QUIET);
					  }
					} else {
					    play_sound(SFX_KNOCK, in_viewport(x, y) ? SND_NORM : SND_QUIET);
					}
				    }
				    board[x][y].returnlaser = 1;
				}
			    }
			}
			break;
						/*********************/
						/* BIG_BOOM logic    */
						/*********************/
		    case BIG_BOOM:
			redraw_field(x, y);
			if (board[x][y].state < 4) {
			    board[x][y].state++;
			    SET_MOVED(x, y, DELAY_BIGBOOM);
			} else {

			    i = board[x][y].id_questionmark;	/* This is why create_object() doesn't clear this property */
			    if ((i > 0) && robbo.alive) {
				clear_field(x, y);
				if (i == QUESTIONMARK)
				    board[x][y].id_questionmark = random_id();
				create_object(x, y, i);
				switch (i) {
				case CAPSULE:
				    open_exit();
				    break;
				case GUN:
				    board[x][y].rotable = 1;
				    board[x][y].randomrotated = 1;
				    board[x][y].direction = rand() & 0x03;
				    break;
				default:
				    break;
				}
			    } else
				clear_field(x, y);
			}
			break;
						/*********************/
						/* RADIOACTIVE_FIELD logic */
						/*********************/
		    case RADIOACTIVE_FIELD:
			negate_state(x, y);
			SET_MOVED(x, y, DELAY_RADIOACTIVE_FIELD);
			break;
		
						/*********************/
						/* TELEPORT logic    */
						/*********************/
		    case TELEPORT:
			negate_state(x, y);
			SET_MOVED(x, y, DELAY_TELEPORT);
			break;
						/*********************/
						/* TELEPORTING logic */
						/*********************/
		    case TELEPORTING:
			redraw_field(x, y);
			if (board[x][y].state < 4) {
			    board[x][y].state++;
			    SET_MOVED(x, y, DELAY_TELEPORTING);
			} else {
			    clear_field(x, y);
			}
			break;
						/*********************/
						/* PUSH_BOX logic    */
						/*********************/
    }
    return 0;
}

/* GBC: slice of update_game()'s object switch, split out so SDCC can
   compile it.  Case bodies are verbatim; bare `return;` -> `return 1;`
   (signals robbo died / stop this cycle). */
int upd_g5(int x, int y) __banked
{
    int sflag, temp_direction, i;
    struct Coords coords, coords_temp;
    switch (board[x][y].type) {
		    case PUSH_BOX:
			if (board[x][y].state == 1) {	/* box is moving */
			    set_coords(&coords, x, y);
			    if (!update_coords (&coords, board[x][y].direction)) {
				if (board[coords.x][coords.y].type == EMPTY_FIELD) {
				    move_object(x, y, coords);
				} else {
				    shoot_object(x, y,board[x][y].direction);
				    board[x][y].state = 0;
				    board[x][y].redraw= 1;
				}
			    } else
				board[x][y].state = 0;
				board[x][y].redraw= 1;
			    if (in_viewport(x, y))
				play_sound(SFX_BOX, SND_NORM);	/* play the sound */
			  else
				play_sound(SFX_BOX,SND_QUIET);
			    SET_MOVED(coords.x, coords.y, DELAY_PUSHBOX);
			}
			break;
						/*********************/
						/* MAGNET logic      */
						/*********************/
		    case MAGNET:
			i = 0;
			if (robbo.blocked == 1)
			    break;	/* *neurocyp we are already locked on a magnet we ignore other blocks */
			set_coords(&coords, x, y);
			   if (board[x][y].rotable) {	/* *neurocyp: we will not rotate, when we shoot */
				if (board[x][y].rotated == 0) {
				    temp_direction=rand() & 0x03;
				    board[x][y].direction = temp_direction;
				    board[x][y].state = temp_direction;
				    board[coords.x][coords.y].rotated = DELAY_ROTATION;
				    redraw_field(coords.x, coords.y);
				}
			    }
			while (i == 0) {
			    if (!update_coords
				(&coords, board[x][y].direction)) {
				if (robbo.x == coords.x
				    && robbo.y == coords.y) {
				    robbo.blocked = 1;
				    robbo.blocked_direction =
					(board[x][y].direction + 2) & 0x03;
				} else
				    switch (board[coords.x][coords.y].type) {
					/*
					 * this things don't protect from
					 * magnet 
					 */
				    case EMPTY_FIELD:
					break;
				    default:
					i = 1;
					break;
				    }

			    } else
				i = 1;
			}
			break;
						/*********************/
						/* GUN logic         */
						/*********************/
		    case GUN:

			set_coords(&coords, x, y);
#ifdef DEBUG_MONITOR_OBJECT_PROCESSING
			printf
			    ("cycle_count=%i: board[%i][%i].processed=%i\n",
			     cycle_count, x, y, board[x][y].processed);
#endif
			sflag = 0;	/* shooting flag */
			/*
			 * we need to make sure this would not rotate nor
			 * move, when it is shooting solid laser 
			 */
			set_coords(&coords_temp, coords.x, coords.y);
			update_coords(&coords_temp, board[coords.x][coords.y].direction);

			if (board[coords_temp.x][coords_temp.y].direction == board[coords.x][coords.y].direction)
			    switch (board[coords_temp.x] [coords_temp.y].type) {
			    case BLASTER:
			    case SOLID_LASER_L:
			    case SOLID_LASER_D:
			    case LASER_L:
			    case LASER_D:
				sflag = 1;	/* *neurocyp: we are not supposed to move nor rotate */
				break;
			    }
			if (sflag == 0) {
			    if (board[coords.x][coords.y].movable) {	/* *neurocyp: no move during solid laser or blaster shot */


				if (can_move(coords, board[x][y].direction2)) {	/* *neurocyp: here probably we should implement stopping during solid laser or blaster shot */
				    update_coords(&coords, board[x][y].direction2);
				    move_object(x, y, coords);
				    SET_MOVED(coords.x, coords.y, DELAY_GUN);
				    board[coords.x][coords.y].state = board[x][y].direction|0x04;
					
				} else {
				    board[coords.x][coords.y].direction2 = (board[coords.x] [coords.y].direction2 + 2) & 0x03;
				    board[coords.x][coords.y].state = board[x][y].direction|0x04;
				    SET_MOVED(coords.x, coords.y, DELAY_GUN);
				}
			    }
			    if (board[coords.x][coords.y].rotable) {	/* *neurocyp: we will not rotate, when we shoot */
				if (board[coords.x][coords.y].rotated == 0) {

				    if (board[coords.x] [coords.y].randomrotated) {
					temp_direction=rand() & 0x03;
					if(board[coords.x][coords.y].movable==1) {
						board[coords.x][coords.y].direction2=abs(board[coords.x][coords.y].direction2+(temp_direction-board[coords.x][coords. y].direction)) % 4;
					} 
					board[coords.x][coords. y].direction = temp_direction;
				    } else {
					board[coords.x][coords. y].direction = (board[coords.x] [coords.y].direction + 1) & 0x03;
				    }
				    board[coords.x][coords.y].rotated = DELAY_ROTATION;
				    redraw_field(coords.x, coords.y);
				}
			    }
			}
			if (board[coords.x][coords.y].shooted == 0) {
			    if (board[coords.x][coords.y].solidlaser == 2) {
				if (((rand()) & 0x07) == 0) {	/* it's blaster */
				    set_coords(&coords_temp, coords.x, coords.y);
				    update_coords(&coords_temp, board[coords.x][coords. y].direction);
				    if (robbo.x == coords_temp.x && robbo.y == coords_temp.y) {
					kill_robbo();
					return 1;	/* Thunor: Exiting here stops new blasters from being created */
				    }
				    switch (board[coords_temp.x] [coords_temp.y].type) {
				    case BLASTER:
				    case TELEPORT:
				    case MAGNET:
				    case WALL:
				    case BOX:
				    case PUSH_BOX:
				    case CAPSULE:
				    case SCREW:
				    case STOP:
				    case RADIOACTIVE_FIELD:
				    case KEY:
				    case DOOR:
				    case GUN:
					board[coords.x][coords.y].shooted = DELAY_LASER;
					break;
				    case BOMB2:
				    case BOMB:
					SET_BLOWED(coords_temp. x, coords_temp.y, DELAY_LASER);
					break;
				    default:

					clear_field(coords_temp.x, coords_temp.y);
					create_object(coords_temp.x, coords_temp.y, BLASTER);
					board[coords_temp.x][coords_temp.y].direction = board[coords.x][coords.y].direction;	/* we allow moving blaster guns! */
					board[coords_temp.x][coords_temp. y].direction2 = board[coords.x][coords. y].direction;
					SET_MOVED(coords_temp.x, coords_temp. y, DELAY_BLASTER);
					board[coords.x][coords.y].shooted = DELAY_LASER;
				    }
				} else
				    board[coords.x][coords.y].shooted = DELAY_LASER;
			    } else {
				if ((rand() & 0x07) == 0) {
				    /*
				     * gun shoots 
				     */
				    /* Atari DZ1 fires audibly; DZ2 solid beams are silent. */
				    if (board[coords.x][coords.y].solidlaser == 0)
					play_sound(SFX_GUN, in_viewport(x, y) ? SND_NORM : SND_QUIET);
				    shoot_object(coords.x, coords.y, board[coords.x][coords.y]. direction);
				} else
				    board[coords.x][coords.y].shooted = DELAY_LASER;
#ifdef DEBUG_MONITOR_OBJECT_PROCESSING
				printf ("--cycle_count=%i: board[coords.x=%i][coords.y=%i].processed=%i\n", cycle_count, coords.x, coords.y, board[coords.x][coords.y].processed);
				if (board[coords.x][coords.y].processed != cycle_count)
				    printf ("--OBJECT NOT MARKED AS PROCESSED FOR THIS CYCLE! <<<<<<<<<<\n");
				/*
				 * printf("--cycle_count=%i:
				 * board[dest.x=%i][dest.y=%i].processed=%i\n", 
				 * cycle_count, dest.x, dest.y,
				 * board[dest.x][dest.y].processed); 
				 */
				/*
				 * if (board[dest.x][dest.y].processed !=
				 * cycle_count) printf("--OBJECT NOT
				 * MARKED AS PROCESSED FOR THIS CYCLE!
				 * <<<<<<<<<<\n"); 
				 */
#endif
			    }
			}
			if (board[coords.x][coords.y].type==GUN && board[coords.x][coords.y].movable==1) 
				board[coords.x][coords.y].state = board[coords.x][coords.y].direction +0x04; /* neurocyp: we want movable guns to have different icons */
			else
				board[coords.x][coords.y].state = board[coords.x][coords.y].direction;
			break;

    }
    return 0;
}

/* moved from board.c (HOME overflow): bomb explosions, banked */
void
blow_bomb(int x, int y) __banked
{
    struct Coords   coords,
                    laserpath;
    int             i,x1,y1;
    if (robbo.alive) {
	if (in_viewport(x, y))
	    play_sound(SFX_BOMB, SND_NORM);
	else
	    play_sound(SFX_BOMB, SND_QUIET);
	}
    if(!robbo.alive) { /* neurocyp: Robbo is not alive, we do not blow the bomb, we just create BIG_BOOM in its place */
	create_object(x, y, BIG_BOOM);
	SET_MOVED(x, y, DELAY_BIGBOOM);
	return;
	}	
    for (i = 0; i < 9; i++) { /* neurocyp: here there was an ugly switch statement, now replaced with a little formula */
	x1=(((i%3)>1)?-1:(i%3));
	y1=((((i/3)%3)>1)?-1:((i/3)%3));
	coords.x=x+x1;
	coords.y=y+y1;
	/*
	 * Robbo was near blowing up 
	 */
	if ((robbo.x == coords.x) && (robbo.y == coords.y)) {
	    kill_robbo();
	}

	/*
	 * Blow-up the object 
	 */
	if (!coords_out_of_range(coords) && !board[coords.x][coords.y].blowed && board[coords.x][coords.y].blowable) {
	    if (board[coords.x][coords.y].type == GUN) {
		SET_BLOWED(coords.x, coords.y, 1);
		SET_MOVED(coords.x, coords.y, DELAY_BOMB_TARGET);
		/*
		 * Thunor: In Atari Robbo the solid laser fire is left
		 * live when the gun is destroyed which is a bit
		 * nonsensical so I have introduced an rcfile overrideable 
		 * game_mechanic.sensible_solid_lasers 
		 */
		if (board[coords.x][coords.y].solidlaser
		    && game_mechanics.sensible_solid_lasers) {
		    set_coords(&laserpath, coords.x, coords.y);
		    while (!update_coords(&laserpath,(board[coords.x][coords.y].direction))  && 
		    (board[laserpath.x][laserpath.y].type ==  LASER_D || board[laserpath.x][laserpath.y].type ==  LASER_L)
			   && board[laserpath.x][laserpath.y].solidlaser  && board[laserpath.x][laserpath.y].direction == board[coords.x][coords.y].direction) 
			   {
			SET_BLOWED(laserpath.x, laserpath.y, 1);
			SET_MOVED(laserpath.x, laserpath.y, DELAY_BOMB_TARGET);
		    }
		}
		/*
		 * Thunor: In Atari Robbo the solid laser is not blowable
		 * but in GNU Robbo there is no solid laser type, so I
		 * need to check the solidlaser property here 
		 */
	    } else
		if (!
		    ((board[coords.x][coords.y].type == LASER_D
		      || board[coords.x][coords.y].type == LASER_L) && board[coords.x][coords.y].solidlaser)) { 
		SET_BLOWED(coords.x, coords.y, 1);
		SET_MOVED(coords.x, coords.y, DELAY_BOMB_TARGET);
		board[coords.x][coords.y].id_questionmark = 0;	/* blowed questionmark doesn't uncover */
	    }
	}
    }
}

/*  Robbo Alex has at leas two different bomb types, here is the place, where we blow these */
/* routine is very similar to regular bomb */
void
blow_bomb2(int x, int y) __banked
{
    struct Coords   coords, laserpath;
    int i,direction=-1,x1,y1;

    if (robbo.alive) {
	if (in_viewport(x, y))
	    play_sound(SFX_BOMB, SND_NORM);
	else
	   play_sound(SFX_BOMB, SND_QUIET);
	}
    if(!robbo.alive) { /* neurocyp: Robbo is not alive, we do not blow the bomb, we just create BIG_BOOM in its place */
	create_object(x, y, BIG_BOOM);
	SET_MOVED(x, y, DELAY_BIGBOOM);
	return;
	}	
    for (i = 0; i < 9; i++) {
	x1=(((i%3)>1)?-1:(i%3));  /* neurocyp: move on x axis */
	y1=((((i/3)%3)>1)?-1:((i/3)%3)); /* neurocyp: move on y axis */
	coords.x=x+x1;
	coords.y=y+y1;
	if(x1==0 || y1==0) direction=(x1!=0)?(-x1+1):(-y1+2);	  /* calculate a direction */
	/* Robbo was near the bomb */
	if ((robbo.x == coords.x) && (robbo.y == coords.y)) {
	    kill_robbo();
	}

	/*
	 * Blow-up the object 
	 */
	if(x1!=0 && y1!=0)
	{
	    if (!coords_out_of_range(coords) && !board[coords.x][coords.y].blowed && board[coords.x][coords.y].blowable) 
		{
		if (board[coords.x][coords.y].type == GUN) 
		{
		    SET_BLOWED(coords.x, coords.y, 1);
		    SET_MOVED(coords.x, coords.y, DELAY_BOMB_TARGET);
		/*
		 * Thunor: In Atari Robbo the solid laser fire is left
		 * live when the gun is destroyed which is a bit
		 * nonsensical so I have introduced an rcfile overrideable 
		 * game_mechanic.sensible_solid_lasers 
		 */
		    if (board[coords.x][coords.y].solidlaser && game_mechanics.sensible_solid_lasers) 
			{
			set_coords(&laserpath, coords.x, coords.y);
			while (!update_coords(&laserpath,(board[coords.x][coords.y].direction))  && 
				(board[laserpath.x][laserpath.y].type ==  LASER_D || board[laserpath.x][laserpath.y].type ==  LASER_L)
			   	&& board[laserpath.x][laserpath.y].solidlaser  && board[laserpath.x][laserpath.y].direction == board[coords.x][coords.y].direction) 
			   {
			    SET_BLOWED(laserpath.x, laserpath.y, 1);
			    SET_MOVED(laserpath.x, laserpath.y, DELAY_BOMB_TARGET);
			}
		    }
		/*
		 * Thunor: In Atari Robbo the solid laser is not blowable
		 * but in GNU Robbo there is no solid laser type, so I
		 * need to check the solidlaser property here 
		 */
		} else
		    if (!((board[coords.x][coords.y].type == LASER_D  || board[coords.x][coords.y].type == LASER_L)  && board[coords.x][coords.y].solidlaser)) 
			{
			SET_BLOWED(coords.x, coords.y, 1);
			SET_MOVED(coords.x, coords.y, DELAY_BOMB_TARGET);
			board[coords.x][coords.y].id_questionmark = 0;	/* blowed questionmark doesn't uncover */
			}
		}
	} else
 		{ /* we have to shoot in 4 directions */
			shoot_object(x ,y, direction);
			SET_MOVED(coords.x, coords.y, DELAY_BOMB_TARGET);
			if(i<7) board[x][y].shooted=0; /* yes, we want to shoot again this tiime, really fast, that is why we reset this */
		}
     }
}

/*****************************************************/
/**** Check Object If Blowed *************************/
/*****************************************************/
/*
 * Thunor: NOTE that the object that is being created here is not cleared
 * beforehand. The reason for this I have found is because once it has
 * blown-up, its id_questionmark property is checked to see if it was a
 * questionmark and if it needs to be recreated 
 */


/* Find, in a single board scan, the teleport of group `teleportnumber` whose
   teleportnumber2 is the smallest value strictly greater than `after`; if there
   is none, wrap to the smallest teleportnumber2 in the group.  Returns the
   chosen teleportnumber2 (>=0) and sets *coords, or -1 if the group is empty.

   This replaces move_robbo's old per-id probing, which called find_teleport for
   every id in (after, MAX_TELEPORT_IDS] plus the -1..after wrap - up to ~15 full
   board rescans when entering a higher-id teleport (e.g. the right one of a
   left/right pair, id 1, searching ids 2..15 then 0).  On the software-multiply
   sm83 that whole-board re-scanning was a visible pre-teleport stall. */
int
find_next_teleport(struct Coords *coords, int teleportnumber, int after) __banked
{
    int i, j, t2;
    int best_gt = 0x7FFF, best_any = 0x7FFF;
    int gx = -1, gy = -1, ax = -1, ay = -1;

    for (i = 0; i < level.w; i++)
	for (j = 0; j < level.h; j++) {
	    if (board[i][j].type != TELEPORT)
		continue;
	    if (board[i][j].teleportnumber != teleportnumber)
		continue;
	    t2 = board[i][j].teleportnumber2;
	    if (t2 < best_any) { best_any = t2; ax = i; ay = j; }
	    if (t2 > after && t2 < best_gt) { best_gt = t2; gx = i; gy = j; }
	}

    if (gx >= 0) { set_coords(coords, gx, gy); return best_gt; }
    if (ax >= 0) { set_coords(coords, ax, ay); return best_any; }
    return -1;			/* no teleport of this group on the board */
}

/* moved from board.c (HOME overflow): questionmark init + random id, banked */
void
init_questionmarks(void) __banked
{
    int             i,
                    j;

    for (i = 0; i < level.w; i++)
	for (j = 0; j < level.h; j++) {
	    if (board[i][j].type == QUESTIONMARK) {
		board[i][j].id_questionmark = random_id();
	    }
	}
}

/***************************************************************************
 * Returning really random object                                          *
 ***************************************************************************/
/*
 * Function returning random object id 
 */

int
random_id(void) __banked
{
    int             ids[11] =
	{ EMPTY_FIELD, PUSH_BOX, SCREW, BULLET, KEY, BOMB, GROUND,
	BUTTERFLY, GUN,
	QUESTIONMARK, CAPSULE
    };

    if (game_mechanics.sensible_questionmarks) {
	return ids[my_rand() % 10];
    } else {
	return ids[my_rand() % 11];
    }
}
