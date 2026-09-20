# Game-loop performance

Level 4 now advances **16.37 game updates per second**, up from **8.34**
(about **96% faster**). Camera scrolling still advances on every display frame.
These are game-logic update rates, not host-emulator speed or display FPS.

## Measurements

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
- Count elapsed VBlanks toward the existing three-frame update gate. Frames
  consumed by logic and rendering count toward the wait. One update runs per
  iteration, overdue updates do not accumulate, and all menu/load paths reset
  the clock. The light-room ceiling remains about 19.9 updates/second.
- Pace ambient tile animation by elapsed VBlanks too. Finish every upload batch
  before flipping frames, retaining the six-tile upload limit.
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
frame-clock wrap tests exercise elapsed-frame pacing.

## Reproduce

A built Coffee GB core and its dependency JARs must be on the absolute
`PERFORMANCE_TEST_CP` classpath. Generate linker symbols from the same ROM:

```sh
make clean
make LCCFLAGS_EXTRA='-Wl-m -Wl-j'
java -Dperformance.assertRowCounts=true --class-path "$PERFORMANCE_TEST_CP" \
  tools/PerformanceTest.java build/robbo.gbc build/robbo.noi \
  1,4,11,16,27,35,45,50,51,54 128 build/performance-after
```

Run an earlier ROM with its own matching `.noi` and a different output directory,
then compare the saved per-tick snapshots with `diff -rq`. The previous engine's
row counts were approximate, so omit the exact-count assertion for that build.
The harness resolves runtime addresses from symbols; its documented field
offsets assume the current 14-byte object layout.

Optional `-Dperformance.initialCycle=257` seeds both the cycle counter and
processed stamps at load; values 1, 257, 513, and 65521 verify the same behavior
across stamp epochs and counter wrap. `-Dperformance.initialFrame=65520` seeds
`sys_time` before loading the room to exercise the VBlank clock rollover.
