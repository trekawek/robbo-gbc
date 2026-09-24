/* Live 8x8-cell view. Map 1 shares its first two rows with the HUD; its
   remaining visible rows hold the board, using compact graphics in VRAM bank 1.
   Keep map 0 current as well so releasing B immediately restores the camera. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "game.h"
#include "render.h"

volatile unsigned char gr_overview_requested;
unsigned char gr_overview_top;
static unsigned char overview_valid;

void overview_gr_reset(void) __banked {
    gr_overview_requested = 0;
    overview_valid = 0;
}

void overview_gr_update(unsigned char show) __banked {
    unsigned char row, count, x, y, full, down;
    unsigned char tiles[20], attrs[20], t[4], a[4];
    unsigned char left = (20 - (unsigned char)level.w) >> 1;
    int top, bottom, source;
    const struct object *p;
    unsigned char *from, *to;
    if (!show) {
        gr_overview_requested = 0;
        overview_valid = 0;
        return;
    }
    top = robbo.y - 8;
    bottom = level.h - 16;
    if (bottom < 0) bottom = 0;
    if (top > bottom) top = bottom;
    if (top < 0) top = 0;
    full = !overview_valid || gr_overview_top != (unsigned char)top;
    if (full) {
        down = overview_valid && top < gr_overview_top;
        row = down ? 15 : 0;
        for (count = 0; count < 16; count++) {
            y = (unsigned char)top + row;
            source = (int)row + top - gr_overview_top;
            if (overview_valid && source >= 0 && source < 16) {
                /* Scroll overlapping rows in place, starting at the edge
                   that cannot overwrite a source row still needed below.
                   Pending object changes are drawn by show_game_area(). */
                from = (unsigned char *)(0x9C40 + (unsigned int)source * 32 + left);
                to = (unsigned char *)(0x9C40 + (unsigned int)row * 32 + left);
                vmemcpy(to, from, (unsigned char)level.w);
                VBK_REG = 1;
                vmemcpy(to, from, (unsigned char)level.w);
                VBK_REG = 0;
            } else {
                for (x = 0; x < 20; x++) {
                    tiles[x] = 0x40;   /* empty glyph, border/HUD background */
                    attrs[x] = 8 | 6;
                }
                if (y < (unsigned char)level.h) {
                    p = &board[0][y];
                    for (x = 0; x < (unsigned char)level.w; x++, p += MAX_H) {
                        render_gr_cell_tiles(p, x, y, t, a);
                        tiles[left + x] = t[0];
                        attrs[left + x] = 8 | a[0];
                    }
                }
                set_tiles(0, row + 2, 20, 1, (unsigned char *)0x9C00, tiles);
                VBK_REG = 1;
                set_tiles(0, row + 2, 20, 1, (unsigned char *)0x9C00, attrs);
                VBK_REG = 0;
            }
            if (down) row--; else row++;
        }
    }
    /* Publish only after every newly visible row is ready. */
    gr_overview_top = (unsigned char)top;
    overview_valid = 1;
    gr_overview_requested = 1;
}

int show_game_area(void) __banked {
    struct object *p = &board[0][0];
    unsigned char t[4], a[4], x, y, normal, overview, attr;
    unsigned char left = (20 - (unsigned char)level.w) >> 1;
    for (x = 0; x < MAX_W; x++) {
        for (y = 0; y < MAX_H; y++, p++) {
            if (!p->redraw) continue;
            p->redraw = 0;
            normal = slot_owner[y & 15] == y;
            overview = gr_overview_requested && x < (unsigned char)level.w
                && y < (unsigned char)level.h && y >= gr_overview_top
                && y < gr_overview_top + 16;
            if (!normal && !overview) continue;
            render_gr_cell_tiles(p, x, y, t, a);
            if (normal) {
                set_tiles(x * 2, (y & 15) * 2, 2, 2, (unsigned char *)0x9800, t);
                VBK_REG = 1;
                set_tiles(x * 2, (y & 15) * 2, 2, 2, (unsigned char *)0x9800, a);
                VBK_REG = 0;
            }
            if (overview) {
                set_tiles(left + x, y - gr_overview_top + 2, 1, 1,
                          (unsigned char *)0x9C00, t);
                attr = 8 | a[0];
                VBK_REG = 1;
                set_tiles(left + x, y - gr_overview_top + 2, 1, 1,
                          (unsigned char *)0x9C00, &attr);
                VBK_REG = 0;
            }
        }
    }
    return 0;
}
