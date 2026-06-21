#ifndef PLAYER_H
#define PLAYER_H

/* Process Robbo at his cell (cx,cy) during the object sweep: read `keys`, then
   shoot (fire held) or move/push.  Matches the original MFAC handler. */
void player_act(unsigned char cx, unsigned char cy, unsigned char keys);

#endif
