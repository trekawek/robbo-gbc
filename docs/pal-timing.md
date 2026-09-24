# Atari PAL gameplay timing

The optimized GBC build's three-frame gate allowed movement to run faster than
Atari PAL. A fractional VBlank clock now targets **14.245927 engine updates/s**.
Two updates move Robbo or a normal moving object one cell, giving **140.391 ms
per cell**, the original seven-PAL-frame interval.

## Reference

In the original `robbo/d1/R1.ASM`, `CHNGCV` waits for `TMR` to reach zero, reloads
it with 7, then increments `CNTR`. The PAL reference clock is 1,773,447 Hz with
114 clocks per line and 312 lines per frame: 49.860746 frames/s. Held movement,
monsters, projectiles and barriers normally advance once per board scan.

This was also checked in Atari800 5.2.0, running the original `bin/robbo.xex`
in PAL mode. A breakpoint immediately after `INC CNTR` showed 124 update
intervals across levels 1, 4, 5 and 8, all exactly seven `RTCLOK` frames.
For this source build the monitor breakpoint is `bpc 1f58`; `m 12` reads the
three-byte frame count. Resolve the address from the assembler listing if the
Atari source changes.

The GBC engine retains GNU Robbo's delay representation: normal movement has a
delay of two engine ticks. Each tick therefore lasts 3.5 PAL frames, or
70.195501 ms. At the GBC's 59.727501 display frames/s this is **4.192602 GBC
frames**, so simply waiting four or five frames would still give the wrong speed.

## Clock and rendering

The HOME VBlank handler accumulates 5488/23009 ticks per GBC frame. Its frequency
error relative to the exact clock ratio is below 0.000006%. The main loop consumes
a single pending tick; overload coalesces missed ticks instead of building a
queue. Menus, restarts, warps and level changes reset pending work and phase.
Long or busy updates can delay gameplay, but do not increase its average speed.

The same clock toggles the ambient font frame every four half-steps, matching
Atari `CHNMON`'s `CNTR & 2` cadence of **14 PAL frames / 280.782 ms**. Uploads still
finish in batches of at most six tiles. Sound has its own PAL clock, and camera
scrolling continues on every GBC VBlank.

This corrects the overall gameplay pace and ambient animation. Magnets now scan
once per two GBC ticks (one Atari board scan) and pull a captured Robbo once per
four ticks (two Atari scans). The inherited GNU engine's other per-object rules,
including shooting cooldown and rotating-gun behavior, remain in place.

The level-56 upper-right bear/magnet regression uses the live GBC ROM. After the
bear leaves the magnet's row, Robbo can press UP during the intervening half
step and leave the row before the magnet scans again. It also checks that
capture and subsequent pulls use the Atari cadence.

## Verification

Build with matching linker symbols and use a built Coffee GB core plus its
dependencies on `TIMING_TEST_CP`:

```sh
make clean
make LCCFLAGS_EXTRA='-Wl-m -Wl-j'
java -Dtiming.assertPal=true --class-path "$TIMING_TEST_CP" \
  tools/TimingTest.java build/robbo.gbc
```

The timing regression exercises the real scheduler, held movement, busy rooms,
counter wrap and pause/resume. It measures emulated clock ticks, not host time.
Individual event intervals are quantized to GBC frames and include rendering
latency; compare the average over many moves with the PAL reference.

Measured steady held movement averaged **140.650 ms/cell**, within one GBC
frame of accumulated error across 20 intervals versus PAL's **140.391 ms**.
Quiet rooms, level 4 and level 51 passed the average update-rate checks; ambient
animation, counter wrap, pause/resume and camera regressions also passed.
All 129 board snapshots in each of ten representative rooms still match the
previous optimized build, including exact active-row counts.

The busy level-50 workload remains CPU-limited at about 12.31 engine updates/s;
the clock limits excess speed but cannot make overloaded scenes meet the target.
