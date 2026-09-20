#ifndef SOUND_H
#define SOUND_H

/* Sound ids match the original Robbo SOUND_ argument order (R1.ASM TABS). */
enum {
    SND_EXPLODE  = 0,   /* wybuch    - bomb blast            */
    SND_SHOOT    = 1,   /* strzal    - fire a bullet         */
    SND_KNOCK    = 2,   /* stuk      - bullet hits a wall    */
    SND_TELEPORT = 3,   /* teleport                          */
    SND_SCREW    = 4,   /* srubka    - collect a screw       */
    SND_LIFE     = 5,   /* extra life                        */
    SND_DOOR     = 6,   /* drzwi     - open a door           */
    SND_AMMO     = 7,   /* naboje    - collect ammo          */
    SND_PUSH     = 8,   /* skrzynia  - push a crate          */
    SND_KEY      = 9,   /* klucz     - collect a key         */
    SND_DESTROY  = 10,  /* wybuszek  - destroy monster/death */
    SND_ENTER    = 11,  /* wejscie                           */
    SND_WIN      = 12,  /* finished a level                  */
    SND_CAPSULE  = 13,  /* otwarcie wyjscia - exit opens     */
    SND_MAGNET   = 14   /* przyciaganie - magnet pull        */
};

/* Banked public API. snd_init installs the independent PAL-paced VBlank
   player; snd_play queues an effect on its original Atari logical voice. */
void snd_init(void) __banked;
void snd_play(unsigned char id) __banked;
void snd_update(void) __banked;   /* compatibility no-op: VBlank advances effects */
void snd_stop(void) __banked;     /* silence and cancel all effects */

#endif
