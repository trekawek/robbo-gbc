/* Isolated driver for SoundTest.java; link with the production sound.o.
 * Mailbox addresses are resolved from this ROM's generated .noi file.
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "sound.h"

volatile unsigned char sound_test_command;
volatile unsigned char sound_test_id;
volatile unsigned char sound_test_ready;
volatile unsigned char sound_test_frames;
volatile unsigned char sound_test_pause;
volatile unsigned char sound_test_bank_result;

unsigned char sound_test_banked_wait(void) __banked;

void main(void) {
    DISPLAY_ON;
    snd_init();
    snd_stop();
    sound_test_ready = 0xa5;
    while (1) {
        vsync();
        if (!sound_test_pause) snd_update();
        if (sound_test_command == 1) snd_play(sound_test_id);
        else if (sound_test_command == 2) snd_stop();
        else if (sound_test_command == 3) snd_init();
        else if (sound_test_command == 4) sound_test_bank_result = sound_test_banked_wait();
        else if (sound_test_command == 5) cpu_fast();
        sound_test_command = 0;
        sound_test_frames++;
    }
}
