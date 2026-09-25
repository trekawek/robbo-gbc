# Atari outro comparison

The reference is the [Atari playthrough from 18:12](https://www.youtube.com/watch?v=FwgGYOn8vnI&t=1092s),
checked against `CONGR`, `DIWA`, `WAIT`, `COLS`, and `COTX` in the original
`robbo/d1/TITLE.ASM`. The empty scene appears at about 18:17.77; the text fade
starts at about 18:31.77. The previous GBC scene took about 8.5 seconds.

## Animation and sound

`WAIT` falls through into `HALT`, adding one PAL frame to its argument. The
scanline-synchronized `DISP` redraw adds another frame to scene poses: `WAIT 6`
produces an observed eight-frame cadence. Copying only the written delay would
still make the animation too fast.

| Phase | PAL frames | Duration |
|---|---:|---:|
| Empty scene | 51 | 1.02 s |
| Nine walking poses, standing pose, standing pause | 91 | 1.83 s |
| Ship landing, 12 poses | 96 | 1.93 s |
| Robbo waving, 14 complete cycles / 28 poses | 224 | 4.49 s |
| Boarding, nine poses | 72 | 1.44 s |
| Departure, 14 poses | 112 | 2.25 s |
| Pause before text | 51 | 1.02 s |
| **Scene total** | **697** | **13.98 s** |

The GBC uses an absolute fractional VBlank deadline: one PAL frame is
approximately 3287/2744 GBC frames. Drawing work counts toward each interval,
so it cannot add a frame to every pose. Positions are compressed horizontally
and vertically to fit 160×144, while retaining every original animation pose.
The ship covers Robbo as he boards.

The original sound sequence is 5 (entry), 14 (landing), 13 (waving), 11
(departure), 5 (postflight), and 0 (text reveal). Boarding does not restart
sound 5. The existing PAL-paced sound player is unchanged; the corrected scene
gives each cue time to finish. The postflight cue continues into the text fade.
On exit, sound 9 accompanies the wipe and sound 13 continues over the returning
title or resumed game, without a blank-screen delay.

Scene colors come from the same Altirra Default PAL conversion as the levels:
`$00, $32, $C8, $0C`, with inverse glyph color `$74`. Ground is inverse; the
ship switches its normal/inverse colors with the original RTC bit-4 cadence.
The second standing pose is extracted from S.FNT alongside the existing frames.

## Text effect

- Fade the original I.FNT pattern glyph 96 through brightness values 0–14,
  with three PAL frames per step (about 0.90 seconds).
- Rotate its eight rows once per PAL frame, matching the Atari VBlank effect.
- Reveal whole character cells at random, including spaces, over roughly
  three seconds. The border remains animated after the message appears.
- Use an inverse heading and the Atari `$02` text background.
- On the final Start press, sweep a bright horizontal band upward over 117 PAL frames.

The revised English translation occupies two pages within a one-character
border. The first congratulates Robbo on escaping and explains the value of
the plans stored in his memory. START advances to the restored original
publisher message: completing Avalon's first game, looking out for its next
releases, and the closing Avalon slogan. Each page uses the patterned fade and
random reveal; the final START triggers the closing wipe. Holding START cannot
skip the second page. The original explosion cue plays only on the first page.

A separate random generator keeps pause-menu previews from changing subsequent
game randomness. The raster wipe uses the GBC window; its timing and direction
match Atari, with a simplified brightness band.

The intro translation also clarifies that START opens the pause menu and the
player must choose RESTART. It ends with the original author's sign-off;
the title screen already credits Janusz Pelc and Avalon.

## Verification

Build with linker symbols and run the Coffee GB regression as described in
the README:

```sh
make LCCFLAGS_EXTRA='-Wl-m -Wl-j'
java --class-path "$CAMERA_TEST_CP" tools/OutroTest.java \
  build/robbo.gbc build/robbo.noi
```

The regression observes rendered wave poses and their cadence, both complete
text pages, their transitions, the animated border, closing wipe, and return to
the same board with level, score, position, and ammo preserved. It also checks
the normal ending-to-title path and that the closing sound plays after both
returns.
