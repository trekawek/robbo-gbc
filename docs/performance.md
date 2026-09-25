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
15.283 ms. At this stage the room remains below the 14.245927 updates/s target;
the later board-scan pass below restores the target pace in this benchmark.

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

The benchmark also reports `upd_g3` (blasters and other group-3 objects),
`upd_g4` (lasers and other group-4 objects), and `upd_g5` (guns, magnets, and
push boxes). These times are included in
`update_game`, so they must not be added to its total.

Before/after snapshots match on all eleven tested rooms (1, 4, 11, 16, 18,
27, 35, 45, 50, 51, 54), with exact active-row counts checked every update.
All four gameplay behavior assertions also pass. Level 50 benefits too,
improving from 12.314 to 13.705 updates/s; the other tested rooms remain at
the PAL limit.

Level 18 also matches through 128 updates starting at cycle 65521, crossing
both processed-byte and 16-bit counter wrap. All four focused laser fixtures
described below match for 32 updates each, with exact active-row counts.

### Blaster trail optimization (level 38)

Level 38's perimeter cannons create long blaster trails. Each trail segment
checks whether its source cannon still exists by walking backward through the
segments. Using a cell pointer for that walk avoids recalculating the board
address from coordinates at every step. The walk still follows each segment's
direction and checks the same source and trail state.

In the 512-update room-start benchmark, level 38 rises from **13.218 to 14.250
updates/s**, reaching the PAL target. Mean `update_game` time falls from
**49.506 to 32.187 ms**; mean `show_game_area` time stays at about **10.54 ms**.
The complete 513-snapshot board/Robbo trace matches byte for byte, with exact
active-row counts checked every update. Level 22's 192-update live cannon test
also passes.

### Solid beam optimization (level 42)

Level 42 keeps many horizontal solid beams active. Their source checks now walk
board cells with a pointer and a byte-sized axis position, and use the result to
identify the segment next to the gun. The forward hit check also computes its
neighbor directly. Beam state changes no longer request a cell redraw: the
laser glyph depends on object type, while its visible animation is uploaded to
shared tiles independently. Creating, moving, and clearing a beam still redraws
the affected cells.

In the 256-update room-start benchmark, level 42 improves from **12.938 to
14.081 updates/s (+8.8%)**. Mean `update_game` time falls from **47.777 to
39.576 ms**, and mean `show_game_area` from **16.107 to 14.330 ms**. All 257
board/Robbo snapshots match byte for byte, with exact active-row counts at every
update. A 512-update run holds **14.137 updates/s**.

### Smaller board scan and laser coordinates

Further profiling found that the board scan itself spent substantial time
reloading its cell pointer and loop counters from the stack. Moving active-cell
processing into a HOME helper lets the compiler keep the scan's pointer and
counters in registers. Retirement checks now test the movement delay first,
avoiding unnecessary type and activity checks for cells still on cooldown.
The laser forward-neighbor calculation also uses byte-sized coordinates,
retaining its boundary clamping and full Robbo-coordinate comparisons.

Compared with the preceding blaster/beam optimizations, 512-update room-start
runs show:

| Level | Before logic ms/update | After logic ms/update | Before updates/s | After updates/s |
|---:|---:|---:|---:|---:|
| 38 | 32.187 | 26.519 | 14.250 | 14.250 |
| 42 | 39.779 | 32.816 | 14.137 | 14.222 |

Both rooms spend about **18% less time in `update_game`**. Level 38 remains at
the PAL limit, while level 42 is within 0.2% of the target in this workload.
Rendering remains about 10.54 ms and 14.17 ms respectively. This is extra
processing headroom; the PAL clock and object delays are unchanged.

The improvement also helps other rooms. In the 128-update level-18 benchmark,
logic falls from **52.584 to 39.306 ms** and pace rises from **12.811 to
14.263 updates/s**, reaching the limit. All eleven representative rooms now
reach that limit in the room-start benchmark (short samples can read slightly
above the long-run target because of fractional frame boundaries).

All 513 snapshots match on levels 38 and 42, as do all 129 snapshots on each of
the eleven representative rooms. Exact active-row counts pass throughout.
All four gameplay behavior assertions and the 62-update sliding-box test pass.
Levels 18 and 42 also match for 128 updates starting at cycle 65521, crossing
the processed-byte and 16-bit counter wrap. All four focused laser fixtures
match for 32 updates each, including board edges and intersecting beams.
A dispatch-table experiment was discarded after measuring a small regression.

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

Existing camera regression and all four gameplay behavior checks passed. Additional
counter-epoch tests cover the tick-255 boundary and the 16-bit cycle-counter wrap;
frame-clock wrap tests exercise pacing. The current fractional clock does not
depend on the wrapping `sys_time` counter.
