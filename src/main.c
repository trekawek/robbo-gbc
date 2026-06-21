#include <gb/gb.h>
#include <gb/cgb.h>
#include "render.h"
#include "hud.h"
#include "sound.h"
#include "title.h"
#include "game.h"
#include "objects.h"

void main(void) {
    if (_cpu == CGB_TYPE) cpu_fast();   /* CGB double-speed: 2x CPU, LCD unchanged */
    DISPLAY_OFF;
    render_init();
    hud_init();
    snd_init();
    objects_init();

    /* HUD window anchored to the bottom two rows */
    WX_REG = 7;
    WY_REG = HUD_PX;

    title_show();
    game_run();   /* never returns */
}
