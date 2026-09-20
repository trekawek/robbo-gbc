#pragma bank 255
#include <gb/gb.h>
#include "sound.h"
#include "gen/sounds.h"

BANKREF(sound)
BANKREF_EXTERN(sound)

/* Atari SOUND_ gives explosions, shots and impacts their own voices; all
   other effects replace voice 3. Keep their envelopes running independently.
   CH1 plays pure tones, CH3 the sampled poly4 buzz, and CH4 the loudest noise
   voice (the GBC has only one noise generator). */
static const unsigned char *seq[4];
static unsigned char next_step[4];
static unsigned int phase;
static unsigned char installed;
static unsigned char tone_volume, noise_volume, wave_id = 0xFF;

static void silence(void) {
    /* Keep the DACs biased between effects. Switching them off creates a
       full-scale DC edge that overwhelms quiet Atari volume-1 effects.
       Envelope pace zero holds volume zero; bit 3 keeps the DAC powered. */
    NR12_REG = 0x08;
    NR14_REG = 0x80;
    NR22_REG = 0;
    NR32_REG = 0;
    NR30_REG = 0x80;
    NR42_REG = 0x08;
    NR44_REG = 0x80;
    tone_volume = noise_volume = 0;
    wave_id = 0xFF;
}

/* This helper and its tables live in the sound bank. Only sound_vblank calls
   it, after explicitly selecting that bank; ordinary callers use the banked
   public API. Local state is never shared with an interrupted snd_play. */
static void sound_tick(void) {
    unsigned char i, flags, volume, nvol = 0, poly = 0;
    const unsigned char *p, *tone = 0, *wave = 0;

    for (i = 0; i != 4; ++i) {
        if (!seq[i]) continue;
        if (next_step[i] == SND_NSTEPS) {
            seq[i] = 0;
            continue;
        }
        p = seq[i] + (unsigned int)next_step[i] * SND_STEP_BYTES;
        ++next_step[i];
        flags = p[0];
        volume = flags & 15;
        if (!volume) continue;
        if (flags & SND_NOISE) {
            /* Equal volumes favour the shared player-action voice. */
            if (volume >= nvol) { nvol = volume; poly = p[1]; }
        } else if (flags & SND_WAVE) {
            wave = p;
        } else {
            tone = p;
        }
    }

    if (tone) {
        volume = tone[0] & 15;
        NR13_REG = tone[1];
        if (volume != tone_volume) {
            NR12_REG = (unsigned char)(volume << 4);
            NR14_REG = tone[2] | 0x80;
            tone_volume = volume;
        } else {
            NR14_REG = tone[2];
        }
    } else if (tone_volume) {
        NR12_REG = 0x08;
        NR14_REG = 0x80;
        tone_volume = 0;
    }

    if (wave) {
        if (wave[3] != wave_id) {
            wave_id = wave[3];
            NR30_REG = 0;  /* Wave RAM is writable safely while DAC is off. */
            p = SND_WAVES[wave_id];
            for (i = 0; i != 16; ++i) ((volatile unsigned char *)0xFF30)[i] = p[i];
            NR30_REG = 0x80;
            NR32_REG = 0x20; /* Full level: volume is already in the waveform. */
            NR33_REG = wave[1];
            NR34_REG = wave[2] | 0x80;
        } else {
            NR33_REG = wave[1];
            NR34_REG = wave[2];
        }
    } else if (wave_id != 0xFF) {
        NR32_REG = 0;
        wave_id = 0xFF;
    }

    if (nvol) {
        NR43_REG = poly;
        /* Avoid resetting the LFSR on every step with unchanged volume. */
        if (nvol != noise_volume) {
            NR42_REG = (unsigned char)(nvol << 4);
            NR44_REG = 0x80;
            noise_volume = nvol;
        }
    } else if (noise_volume) {
        NR42_REG = 0x08;
        NR44_REG = 0x80;
        noise_volume = 0;
    }
}

/* Runs even during a slow update_game/render pass. 686/3287 steps per GBC
   frame matches four PAL frames (142272 / 1773447 seconds) within 0.00002%.
   Bank switching must be in HOME, saving/restoring the interrupted ROM bank. */
static void sound_vblank(void) __nonbanked {
    unsigned char saved_bank;
    phase += 686;
    if (phase < 3287) return;
    phase -= 3287;
    saved_bank = CURRENT_BANK;
    SWITCH_ROM(BANK(sound));
    sound_tick();
    SWITCH_ROM(saved_bank);
}

void snd_init(void) __banked {
    __critical {
        NR52_REG = 0x80;
        NR51_REG = 0xFF;
        NR50_REG = 0x77;
        NR10_REG = 0;
        NR11_REG = 0x80; /* 50% square matches POKEY $A. */
        NR41_REG = 0;
        seq[0] = seq[1] = seq[2] = seq[3] = 0;
        phase = 0;
        silence();
        if (!installed) {
            add_VBL(sound_vblank);
            installed = 1;
        }
    }
}

void snd_play(unsigned char id) __banked {
    unsigned char voice;
    if (id >= SND_COUNT) return;
    voice = id < 3 ? id : 3;
    /* SOUND_ queues the first step for the next shared sound tick. There is
       no global priority lock, including during a sequence's silent tail. */
    __critical {
        seq[voice] = SND_SEQ[id];
        next_step[voice] = 0;
    }
}

void snd_stop(void) __banked {
    __critical {
        seq[0] = seq[1] = seq[2] = seq[3] = 0;
        silence();
    }
}

/* Kept for existing main/menu callers; timing belongs exclusively to VBlank. */
void snd_update(void) __banked {}
