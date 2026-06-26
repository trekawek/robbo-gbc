#pragma bank 255
#include <gb/gb.h>
#include "sound.h"
#include "gen/sounds.h"

/* Faithful port of the original POKEY sound engine: each effect is a 16-step
   sequence of (frequency, waveform/volume), advanced one step every 4 frames.
   Tonal steps play on square channel 1, noisy steps on noise channel 4.
   (The original mixes up to 4 POKEY voices; here one effect plays at a time.) */

void snd_init(void) __banked {
    NR52_REG = 0x80;   /* sound on */
    NR51_REG = 0xFF;   /* all channels, both sides */
    NR50_REG = 0x77;   /* full volume L/R */
}

static const unsigned char *seq;   /* current effect step data, 0 = idle */
static unsigned char sstep, sframe, last_vbl, cur_prio;

/* The original mixes effects across 4 POKEY voices; with one channel a new
   effect may only interrupt a playing one of equal-or-lower importance, so the
   constant object SFX (shots, tiles destroyed by lasers) don't chop up player
   feedback (pickups, door, win). */
static const unsigned char PRIO[SND_COUNT] = {
    2, /*EXPLODE*/  1, /*SHOOT*/    0, /*KNOCK*/    2, /*TELEPORT*/  3, /*SCREW*/
    3, /*LIFE*/     2, /*DOOR*/     3, /*AMMO*/     2, /*PUSH*/      3, /*KEY*/
    1, /*DESTROY*/  2, /*ENTER*/    3, /*WIN*/      3, /*CAPSULE*/   2  /*MAGNET*/
};

void play_step(void) __banked {
    const unsigned char *p = seq + (unsigned int)sstep * 3;
    unsigned char b0 = p[0], vol = b0 & 0x0F;
    if (vol == 0) {                          /* silence both channels */
        NR12_REG = 0x00;
        NR42_REG = 0x00;
    } else if (b0 & 0x80) {                  /* noise (ch4) */
        NR12_REG = 0x00;
        NR41_REG = 0x00;
        NR42_REG = (unsigned char)(vol << 4);
        NR43_REG = p[1];
        NR44_REG = 0x80;
    } else {                                 /* square tone (ch1) */
        NR42_REG = 0x00;
        NR10_REG = 0x00;                     /* no sweep */
        NR11_REG = 0x80;                     /* 50% duty */
        NR12_REG = (unsigned char)(vol << 4);/* constant volume (no envelope) */
        NR13_REG = p[1];
        NR14_REG = (unsigned char)(0x80 | (p[2] & 0x07));
    }
}

void snd_play(unsigned char id) __banked {
    if (id >= SND_COUNT) return;
    if (seq && PRIO[id] < cur_prio) return;   /* don't cut a more important effect */
    seq = SND_SEQ[id];
    cur_prio = PRIO[id];
    sstep = 0; sframe = 0;
    last_vbl = (unsigned char)sys_time;
    play_step();
}

void snd_stop(void) __banked {
    seq = 0;
    NR12_REG = 0x00;
    NR42_REG = 0x00;
}

/* Advance by the number of VBlanks actually elapsed (not loop iterations), so
   each step lasts exactly 4 frames like the original, regardless of framerate. */
void snd_update(void) __banked {
    unsigned char now;
    if (!seq) return;
    now = (unsigned char)sys_time;
    sframe += (unsigned char)(now - last_vbl);
    last_vbl = now;
    while (sframe >= 4) {
        sframe -= 4;
        if (++sstep >= SND_NSTEPS) {         /* effect finished */
            seq = 0;
            NR12_REG = 0x00;
            NR42_REG = 0x00;
            return;
        }
        play_step();
    }
}
