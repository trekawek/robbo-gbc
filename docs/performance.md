# Game-loop performance

Gameplay now targets **14.245927 game updates per second**, matching the
[Atari PAL movement pace](pal-timing.md). The processing optimizations remain;
camera scrolling still advances on every display frame. These are game-logic
update rates, not host-emulator speed or display FPS.

## Measurements

The table records the optimization comparison **before the PAL pacing correction**
(optimized commit `d8eb905`). Level 4 improved from 8.34 to 16.37 updates/s,
which exposed that the old three-frame limit could exceed Atari's speed.

Measured with Coffee GB using emulated master-clock ticks (4,194,304 per second),
from the real level loader through 128 complete updates. Each room starts in a
fresh emulator with the same game state and no player input. These are repeatable
room-start workloads; timings can change as the player moves and objects interact.

| Level | Before updates/s | After updates/s | Improvement |
|---:|---:|---:|---:|
| 1 | 11.08 | 19.42 | +75% |
| 4 | 8.34 | 16.37 | +96% |
| 11 | 10.69 | 18.56 | +74% |
| 16 | 10.39 | 19.21 | +85% |
| 27 | 8.22 | 16.78 | +104% |
| 35 | 9.34 | 18.61 | +99% |
| 45 | 11.71 | 19.91 | +70% |
| 50 | 6.78 | 12.39 | +83% |
| 51 | 7.75 | 14.94 | +93% |
| 54 | 8.36 | 16.47 | +97% |

For level 4, the average `update_game` time dropped from **50.75 to 33.96 ms**,
and `show_game_area` from **31.31 to 12.82 ms**. Function timings include their
callees and interrupts. Changes to the wait policy provide the remaining gain.

### Cannon and laser optimization (level 18)

With the current PAL pacing enabled, the same 128-update room-start benchmark
for level 18 improves from **8.581 to 11.276 updates/s (+31.4%)**. Average
`update_game` time falls from **88.756 to 61.126 ms**; rendering remains about
15.283 ms. This heavy room still falls below the 14.245927 updates/s target.

Fixed cannons on shot cooldown now synchronize their image directly in the
main scan, avoiding a banked handler call and unnecessary movement/rotation
checks. Their cooldowns and explosion checks still run normally.

Solid beams validate their cannon connection and update their animation by
walking the relevant board axis directly. This avoids repeated coordinate
helper calls and two-axis bounds checks. The connection checks remain quadratic
in beam length; no cached connection is introduced that could become stale when
a cannon moves or is destroyed. Interior outbound segments reuse the forward
neighbor already found for collision checking and skip movement helpers when
that neighbor is another beam segment traveling in the same direction.

The benchmark also reports `upd_g4` (lasers and other group-4 objects) and
`upd_g5` (guns, magnets, and push boxes). These times are included in
`update_game`, so they must not be added to its total.

Before/after snapshots match on all eleven tested rooms (1, 4, 11, 16, 18,
27, 35, 45, 50, 51, 54), with exact active-row counts checked every update.
All four gameplay behavior assertions also pass. Level 50 benefits too,
improving from 12.314 to 13.705 updates/s; the other tested rooms remain at
the PAL limit.

Level 18 also matches through 128 updates starting at cycle 65521, crossing
both processed-byte and 16-bit counter wrap. All four focused laser fixtures
described below match for 32 updates each, with exact active-row counts.

## Changes

- Maintain exact active-cell counts per row, removing the second row scan.
  Activation, replacement, clearing, and retirement all balance the counts,
  including moves into cells already visited by the scan.
- Skip explosion checks for cells without a pending explosion; remove unused
  coordinate setup and other work left over from splitting the object handlers.
- Clear an empty cell directly instead of calling the general object creator
  on every movement. Its question-mark field, flags, and processed stamp retain
  the same meanings.
- Track byte-sized drawing coordinates alongside the cell pointer. This removes
  division/modulo for each dirty cell and repeated address calculations when
  producing its four tiles. Tile/palette output and scan order are preserved.
- Replace the old loop-iteration wait with a VBlank clock. The subsequent PAL
  correction uses fractional periods averaging 4.192602 GBC frames per update,
  with one pending tick and resets after menus/loads. This preserves processing
  headroom while limiting movement to the original speed.
- Pace ambient tile animation from that clock every 14 PAL frames. Finish every
  upload batch before flipping frames, retaining the six-tile upload limit.
- Compare the packed one-byte `processed` stamp with the low byte of the cycle
  counter. Previously, after tick 255, moving objects ahead in scan order could
  be processed twice in one tick. The counter now wraps as an unsigned integer.

Object delays, barrier behavior, and the engine's row-first processing order
are preserved. Existing sound timing runs independently in VBlank.

## Verification

The first 128 updates produced identical board and Robbo state on all ten rooms
before and after optimization (129 snapshots including the initial state).
Snapshots include object behavior, delays, directions, Robbo, game mode, and
restart countdown; they exclude presentation/cache fields and processed stamps.
Exact active-row counts are checked against all cell flags at each boundary.

Existing camera regression and all four gameplay behavior checks pass. Additional
counter-epoch tests cover the tick-255 boundary and the 16-bit cycle-counter wrap;
frame-clock wrap tests exercise pacing. The current fractional clock does not
depend on the wrapping `sys_time` counter.

## Reproduce

A built Coffee GB core and its dependency JARs must be on the absolute
`PERFORMANCE_TEST_CP` classpath. Generate linker symbols from the same ROM:

```sh
make clean
make LCCFLAGS_EXTRA='-Wl-m -Wl-j'
java -Dperformance.assertRowCounts=true --class-path "$PERFORMANCE_TEST_CP" \
  tools/PerformanceTest.java build/robbo.gbc build/robbo.noi \
  1,4,11,16,18,27,35,45,50,51,54 128 build/performance-after
```

Current results include the PAL limit and will differ from the historical speed
table above. Run an earlier ROM with its own matching `.noi` and a different output directory,
then compare the saved per-tick snapshots with `diff -rq`. The previous engine's
row counts were approximate, so omit the exact-count assertion for that build.
The harness resolves runtime addresses from symbols; its documented field
offsets assume the current 14-byte object layout.

Optional `-Dperformance.initialCycle=257` seeds both the cycle counter and
processed stamps at load; values 1, 257, 513, and 65521 verify the same behavior
across stamp epochs and counter wrap. `-Dperformance.initialFrame=65520` seeds
`sys_time` before loading the room to exercise the VBlank clock rollover.

For focused laser regressions, add
`-Dperformance.laserScenario=beams|disconnected|edges|crossings` (choose one).
These replace the loaded room with controlled beam fixtures and isolate Robbo
from them. Use 32 updates, active-row assertions, and separate snapshot
directories for the two ROMs. They cover growing/returning beams in all four
directions, missing/destroyed cannons, gaps, board boundaries, opposing and
perpendicular beams, and mixed laser-type neighbors. For example:

```sh
java -Dperformance.assertRowCounts=true -Dperformance.laserScenario=edges \
  --class-path "$PERFORMANCE_TEST_CP" tools/PerformanceTest.java \
  build/robbo.gbc build/robbo.noi 18 32 build/laser-after
```
