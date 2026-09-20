/* Run from a different ROM bank while sound VBlanks swap the sound bank in.
 * A failed restore either corrupts execution or fails the bank/data checks.
 */
#pragma bank 2
#include <gb/gb.h>

static const unsigned char sentinel[] = { 0x53, 0x46, 0x58, 0x42 };

unsigned char sound_test_banked_wait(void) __banked {
    unsigned char frame;
    for (frame = 0; frame != 90; ++frame) {
        vsync();
        if (CURRENT_BANK != 2 || sentinel[frame & 3] !=
                (frame % 4 == 0 ? 0x53 : frame % 4 == 1 ? 0x46 : frame % 4 == 2 ? 0x58 : 0x42)) {
            return 0;
        }
    }
    return 0xa5;
}
