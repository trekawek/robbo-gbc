#ifndef RENDER_H
#define RENDER_H

struct object;
extern unsigned char slot_owner[16];
extern volatile unsigned char gr_overview_active, gr_overview_requested;
extern unsigned char gr_overview_top;

void render_gr_init(void);
void render_gr_load(void);
void render_gr_camera(void);
void render_gr_vblank(void);
void render_gr_camera_pause(void);
void render_gr_camera_resume(void);
void render_gr_anim(void);
void render_gr_robbo(void);
void render_gr_cell_tiles(const struct object *p, unsigned char x, unsigned char y,
                          unsigned char *tiles, unsigned char *attributes);
void overview_gr_update(unsigned char show) __banked;
void overview_gr_reset(void) __banked;

#endif
