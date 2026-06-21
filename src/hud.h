#ifndef HUD_H
#define HUD_H

void hud_init(void);   /* upload digit tiles, set HUD palette, draw icons */
void hud_icons(void);  /* (re)draw the static icons (after the pause menu) */
void hud_draw(void);   /* refresh counters in the window layer (VBlank only) */

#endif
