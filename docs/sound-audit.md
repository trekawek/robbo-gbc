# Atari / GBC sound audit

All 15 original `R1.ASM TABS` effects were compared with the GBC converter,
player, and gameplay triggers. The reference is PAL Robbo: POKEY at 1,773,447 Hz,
AUDCTL=0, and a sound-table step every four PAL frames (80.223 ms).

## Findings and changes

- **Timing:** the previous player used four GBC frames (66.971 ms), making every
  effect about 16.5% too short. A shared fractional VBlank clock now follows the
  original PAL rate. Individual transitions are quantized to one GBC VBlank;
  long-running timing does not drift. Effects continue through slow gameplay
  and rendering passes instead of skipping intermediate steps to catch up.
- **Waveforms:** `$A` is a square, but `$C` is a sampled four-bit polynomial.
  Treating both as squares made ammo a roughly 16 kHz whistle instead of its
  2.11 kHz buzz and changed the door's bass texture. `$C` now uses custom CH3
  waveforms, including the divisor-dependent five- or fifteen-event periods.
- **Noise:** `$8` uses the full divider event rate, which the old converter
  incorrectly halved. All eight NR43 divisors are now searched, preserving
  changes that the former shift-only conversion collapsed. `$0` is separately
  approximated with the lower bandwidth of poly5-gated noise (15/31 event rate).
- **Envelopes:** original 0–15 volumes replace doubled/clipped volumes. Wave RAM
  includes the step's volume, so ammo and door keep their envelopes without
  relying on CH3's three coarse hardware volume levels. Unchanged pulse/noise
  volume no longer needlessly retriggers the channel every step.
  Muting keeps the DACs powered at zero volume, avoiding the large clicks
  caused by switching a DAC off between quiet effects or silent steps.
- **Overlap:** explosion, shot, and knock retain their separate Atari logical
  voices; IDs 3–14 replace the shared fourth voice. Removed the global priority
  lock, including silent table tails that previously blocked other effects.
  Tones/waves can now continue alongside explosions and shots.
- **Events:** walking is silent; knock belongs to projectile impacts. Added
  those impact sounds, removed extra firing sounds from solid lasers/blasters,
  and restored ENTER on initial, restarted, and warped level loads. Existing
  offscreen sound suppression remains active.

## Every effect

The duration below runs from the first sounding step to the end of the last
sounding step; it includes internal silence. Every source table still has 16
steps. `$0` = gated noise, `$8` = noise, `$A` = square, `$C` = poly4 buzz.

| ID | Effect | Atari modes | PAL span | Main correction |
|---:|---|---|---:|---|
| 0 | Explosion | `$8` | 963 ms | Noise bandwidth, unclipped envelope, independent voice |
| 1 | Shot | `$8` | 241 ms | Preserve three distinct noise rates; independent voice |
| 2 | Knock | `$8` | 80 ms | Correct bandwidth; impacts instead of footsteps |
| 3 | Teleport | `$A` | 1,203 ms | PAL pitch/timing and original fading arpeggio |
| 4 | Screw | `$A` | 321 ms | PAL pitch/timing; shots no longer block pickup |
| 5 | Extra life | `$A` | 1,284 ms | PAL pitch/timing and unclipped volume peaks |
| 6 | Door | `$0`, `$C` | 882 ms | Gated noise and variable-period poly4 bass |
| 7 | Ammo | `$C` | 1,203 ms | 2.11 kHz poly4 buzz, alternating sound/silence |
| 8 | Push | `$0` | 241 ms | Gated-noise bandwidth and original quiet envelope |
| 9 | Key | `$A` | 481 ms | PAL pitch/timing and rising pitch contour |
| 10 | Destroy | `$8` | 562 ms | Correct noise rate and original envelope |
| 11 | Enter | `$8`, `$A` | 1,284 ms | Noise sweep, four-step gap, tone; restored spawn trigger |
| 12 | Win | `$A` | 1,203 ms | PAL timing and unclipped repeated volume accents |
| 13 | Capsule opens | `$0` | 1,284 ms | Gated-noise alternation and unclipped fade |
| 14 | Magnet | `$0` | 1,284 ms | Distinct noise rates and original rising envelope |

Extra-life objects are not represented by this port's existing level converter;
its sound is still available and used in the ending. Normal capsule advancement
keeps WIN playing: this port loads the next room immediately, whereas Atari has
an intervening screen wipe before ENTER.

## Remaining hardware approximations

The GBC has one noise generator, so simultaneous noise voices are represented
by the loudest active one while all logical envelopes keep advancing. It has a
15-bit noise sequence and no POKEY poly5 gate; matching noise bandwidth cannot
make those waveforms identical. Retriggering is needed when pulse/noise volume
changes. CH3 has only 32 four-bit samples, and its 32 Hz minimum clamps three
door steps whose Atari fundamentals are about 18.1, 22.1, and 28.3 Hz. Poly4
phase is also free-running on Atari, so its five-event patterns can vary with
when the effect starts. These limitations would require software sample
streaming/mixing to remove, with a different CPU and audio architecture.

## Reproduce the checks and recordings

Build normally with the bundled GBDK. `SOUND_TEST_CP` must contain a built
Coffee GB core and its dependency JARs, using absolute paths:

```sh
make
make sound-test SOUND_TEST_CP="$SOUND_TEST_CP"
python3 tools/sound_reference.py "$HOME/dev/lkavalon-atari/robbo/d1/R1.ASM"
```

The Java harness links the production player into a small test ROM and reads
mailbox addresses from that ROM's linker symbols. It records all effects to
`build/sound-captures/`, then repeats at CGB double speed in `double-speed/`.
It independently reads the Atari tables and checks timing, silent gaps, waveform
selection, square/poly4 pitch, original volume, and nearest NR43 settings.
Additional checks cover overlapping voices, shared-voice replacement, stop,
invalid IDs, playback without main-loop updates, and restoring the interrupted
ROM bank during a 90-frame banked workload. A channel left enabled at zero
volume is silent: assertions use audible volume as well as enable flags.

The Python reference requires NumPy and SciPy. It renders the original register
sequences with master-clock polynomial sampling, including the poly5 output
hold and latched gate, to `build/sound-test/reference/`. It is an independent
digital POKEY model; it does not claim to reproduce analogue mixer nonlinearities
or a recording of physical Atari hardware.

To generate per-effect Atari / old GBC / new GBC listening triplets from a
saved baseline capture directory:

```sh
python3 tools/compare_sound.py --before /path/to/old-sound-captures
```

This writes WAVs and a measured report under `build/sound-test/comparison/`.
The listening copies equalize RMS to make timbre easier to compare; raw
captures retain their original gain. Spectral distances in the report are
diagnostics, not perceptual quality scores.

Validation for this change: all 15 effects passed at both CPU speeds; camera
regression passed; all four existing in-game behavior checks passed. A separate
host harness exercised 12 gameplay sound-trigger cases against the actual engine.

## Source evidence

- Original Atari [`R1.ASM`](https://github.com/trekawek/atari-robbo/blob/main/robbo/d1/R1.ASM):
  `SOUND_` (fixed voice allocation), `SOUNDV`/`SOPL` (shared cadence and reverse
  playback), `TABS` (all 240 words), `WFAC` (ENTER), and `MFEX` (silent walking).
- Original [`R2.ASM`](https://github.com/trekawek/atari-robbo/blob/main/robbo/d1/R2.ASM):
  `CVBL` calls SOUNDV; `DZ1` emits gunshots, while `DZ2`/`DZ3` firing is silent;
  projectile impacts use sound 2.
- [Atari800 POKEY model](https://github.com/atari800/atari800/blob/master/src/mzpokeysnd.c):
  master-clock polynomial advancement, divider sampling, and poly5 gate latch.
- [Pan Docs audio registers](https://github.com/gbdev/pandocs/blob/master/src/Audio_Registers.md):
  GBC pulse/wave periods, wave RAM, volume retriggering, and noise divisors.
