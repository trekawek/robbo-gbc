#ifndef OBJECTS_H
#define OBJECTS_H

/* Build the active-object lookup table.  Call once at startup. */
void objects_init(void);

/* Clear transient per-level object state (beams, slides).  Call on level load. */
void objects_reset(void);

/* One game tick: a single scan-order sweep over the cave (matching the original
   CHNGCV).  Robbo is processed in place as part of the sweep, using `keys`. */
void objects_update(unsigned char keys);

/* Detonate a bomb at (x,y): 3x3 explosion. */
void bomb_explode(unsigned char x, unsigned char y);

/* Register a pushed object at (x,y) to slide in dir until blocked (inertia). */
void slide_add(unsigned char x, unsigned char y, unsigned char dir, unsigned char b);

/* Resolve a bullet/beam striking cell (x,y) holding object t (destroy monster,
   detonate bomb, reveal '?', clear debris, or be absorbed). */
void obj_bullet_hit(unsigned char x, unsigned char y, unsigned char t);

/* Stamp cell (x,y) as already-processed this tick (the original's $80 flag), so
   a freshly-spawned bullet or a just-moved Robbo isn't stepped again this sweep. */
void obj_mark(unsigned char x, unsigned char y);

#endif
