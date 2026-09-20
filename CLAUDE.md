# Robbo for Game Boy Color — agent guide

A GBC port of **Robbo** (1989, LK Avalon) that runs **GNU Robbo's `board.c` game logic**
with the **authentic Atari 8-bit assets** (font, sound, per-level colours) and the
**authentic Atari level designs**. Built with the bundled GBDK-2020 (SDCC/sm83).

Read `README.md` first for the player-facing description and the rendering model. This
file is the working guide for continuing development.

## External sources (paths matter)

- `ORIG` = `~/dev/lkavalon-atari/robbo` — original Atari game. Supplies the font, sound
  tables, instruction text, and the **authentic level designs** (`d2/C*.txt`).
- `GNUROBBO` = `~/dev/gnurobbo-0.66` — GNU Robbo. Supplies the engine logic we ported and
  `data/levels/original.dat` (used only for per-level colours now, not level geometry).
- `~/dev/coffee-gb` — headless Java GBC emulator used by the `solver/` test harness.
- Reference commit `882ea088c4cdb10f365e26e06cb54260c1d90b49` = the older GBC build whose
  levels were considered "correct" during the Atari-faithfulness work.

Override `ORIG=`/`GNUROBBO=` on the make line if they live elsewhere.

## Build

```sh
make            # regenerate assets + Atari levels, build build/robbo.gbc
make clean      # always clean after editing a .h — header deps aren't fully tracked
```

`make` is the default Atari-levels build. Level pipeline:
`convert_atari_levels.py` (Atari d2 → `src/gen/atari_levels.dat`) →
`convert_gnu_levels.py` (.dat → `src/levels_*.c` banked grids + `levels_data.h`).

board.c / board_upd.c are huge single TUs and compile with capped register allocation
(`--max-allocs-per-node 3000`); a full build is ~1–2 min.

Title logo: `tools/sim_logo.py` reproduces TITLE.ASM's `BIGR` routine bit-for-bit to
extract the authentic 'RoDDo' bitmap (5 glyphs from I.FNT `$3E8`, 1bpp→2bpp doubling
ANDed with the diagonal MASK bevel). `convert_font.py` imports it, scales 2×3 into the
`logo_tiles` (14×3 tiles) and emits `logo_rainbow[15][4]` — one GBC palette per Atari
hue. The banked title can't `SWITCH_ROM` to itself, so `render_gr_logo()` (HOME) copies
the rainbow table into the WRAM `gr_logo_rainbow[]`; `title_gr_show()` cycles it for the
rainbow animation. The Atari rainbow is *temporal* hue-cycling of COLPF0/1/2 (lum 2/4/8),
not a spatial gradient.

## Headless verification (two harnesses)

1. **PyBoy quick screenshot** (`tools/shot.py`): boots the ROM and saves a PNG after a
   scripted input sequence.
   ```sh
   python3 tools/shot.py build/robbo.gbc out.png 200 "120:start:5,200:up:40"
   ```
2. **coffee-gb Java harness** (`solver/`): drives buttons via
   `bus.post(new ButtonPress/ReleaseEvent(Button.X))`, reads WRAM via
   `gb.getAddressSpace().getByte(addr)`, captures frames on `GbcFrameReadyEvent`.
   `solver/run.sh` builds the coffee-gb core jar + compiles/runs `Solver.java`.
   `solver/BehaviorTest.java` is the in-game behaviour assertion suite (verify by
   *observing board state*, not by reading internal flags) — run it after engine changes.
   Other `*.java` in `solver/` are single-purpose probes built during past tasks.

Memory note: never run more than 3 background tasks at once.

### WRAM addresses (for the coffee-gb harness)

GR base `0xC0B1`. Board base `0xC0DB`, **cell stride 14**, `idx = (x*31 + y)`:
- `+0` type, `+2` state, `+3` direction, `+4` direction2, `+5` teleportnumber,
  `+6` teleportnumber2, `+7` solidlaser, `+8` moved, `+9` shooted, `+10` rotated,
  `+11` processed, `+12` bitfield byte A (bit0 destroyable, 1 blowable, 2 killing,
  3 blowed, 4 rotable, 5 randomrotated, 6 movable), `+13` bitfield byte B
  (bit0 shooting, 1 redraw, 2 inlist).
- `cycle_count=0xC0D3`, `game_mode=0xC0D5`, `level=0xC0B5`,
  `level_packs/level_selected=0xC0C3`, `restart_timeout=0xC0D9`.
- robbo `0xDC1A`: `+0` x, `+2` y, `+4` alive, `+8` dir, `+10` screws, `+12` keys,
  `+14` bullets, `+16` moved, `+18` shooted, `+20` exitopened, `+26` teleporting.
- viewport `0xDC36`.

(Re-verify offsets against `board.h`/the map if the struct changes.)

## Source layout

```
src/board.c        GNU Robbo engine — update_game split into upd_g1..g5 (SDCC TU-size limit)
src/board_upd.c    per-object update cases (banked) — most gameplay behaviour lives here
src/board_robbo.c  Robbo move/shoot (banked)
src/board.h        packed 14-byte cell struct + scaled object delays + tile IDs
src/glue.c         GBC shim: main loop, input, tick pacing, RNG, sound bridge, lifecycle
src/render.c       board→tiles+palettes, scrolling viewport, row streaming, sound viewport
src/loader.c       level load → board setup + per-level palette + the [additional] params
src/menu.c         title (authentic 'RoDDo' logo + rainbow) + pause/warp menu (banked)
src/atari_pal.c    56 authentic per-level palettes (banked)
src/levels_*.c     generated level data (HOME index + banked grids) — DO NOT hand-edit
tools/             asset + level converters; shot.py screenshot helper
solver/            coffee-gb Java harness + analysis probes (gitignored, not in repo)
```

## Engine model (key facts)

- Cells: `board[MAX_W=16][MAX_H=31]` struct array, indexed `[x][y]`. Plain `[x][y]` —
  the row-pointer-table experiment was tried and fully reverted.
- `update_game` scans cells each tick. Perf shortcuts: `inlist` bit marks active cells;
  `gr_row_active[y]` lets whole inert rows be skipped. The `y`-then-`x` scan order is
  load-bearing — don't reorder.
- Timing: GBC double-speed (`cpu_fast`), `GR_TICK_GATE=3`, object delays scaled by
  `GR_DELAY_DIV=2` via `SCALE()` (gnu tuned for 25 Hz; GBC reaches ~8 cycles/s).
- Main starts rendering after VBlank; logic (`update_game`) sets redraw flags, then
  `show_game_area` flushes dirtied cells using GBDK's VRAM-safe tile routines. Large redraws
  can extend into active display. The camera eases independently in a small VBlank handler;
  keep its helpers in HOME, publish targets atomically, and stream rows before allowing
  the camera to enter them. Suspend the camera for menus/title/ending.
- Wall shape is per-level: `render_gr_load` picks `wall_chars` group `(level-1)/4`, so the
  wall style changes every 4 levels — the authentic Atari rule (`ENTCV` "murki shp" in
  R1.ASM: `group = (CNUM*4 & 0xF0)>>4`). `convert_font.py`'s 16 wall groups line up 1:1 with
  M.FNT's metatiles, so the index maps straight through. (Was hardcoded to group 0.)
- Robbo faces **down** at level start (`init_robbo` sets `direction = 2`) — matches the
  Atari (`LM = %1101`). Direction encoding: 0=right, 2=down, 4=left, 6=up.

## ⚠️ Gotchas learned the hard way

- **SDCC bitfield-from-variable is a silent no-op.** `cell.rotable = v;` (v a variable)
  compiles to nothing; only **constant** assignments work. In `loader.c` every
  `[additional]` flag must be written as `if (v) cell.flag = 1;`. This bug masked all the
  behaviour flags (movable guns, shooting birds, rotable guns) until found — if a flag
  "isn't taking effect," check for this pattern first.
- **Teleport groups are 1-based.** gnu treats `teleportnumber==0` as "no teleport", so the
  converter maps Atari digit `b` → group `b-0x30+1`. Off-by-one here = teleports that make
  Robbo vanish (he enters and never exits).
- **Teleport destination search is one board scan (`find_next_teleport`), not per-id probing.**
  `teleportnumber2` ids are assigned 0-based in scan order, so the *second* teleport of a
  same-row pair gets id 1. gnu's original loop probed every id in `(id, MAX_TELEPORT_IDS]`
  then wrapped, calling the old `find_teleport` (a full board scan) ~15× when entering a
  higher-id teleport — a visible pre-teleport stall on the software-multiply sm83 (the
  "right teleport lags, left is instant" bug). `find_next_teleport` finds the next id in
  cyclic order in a single scan; the candidate-trial order (and destination) is identical.
- **Atari levels are NOT mirror images of gnu levels** — different glyph alphabets. The
  authentic geometry comes from `convert_atari_levels.py`, validated 56/56 on screw counts.
- **`gr_atari_pal` (src/atari_pal.c) is GENERATED by `tools/gen_atari_pal.py`** from each
  level's authentic colour registers through the **standard atari800 PAL palette** — Robbo
  is a PAL (Polish LK Avalon) game, so PAL is the faithful reference (NTSC renders the same
  bytes bluer/darker). The colour registers are the d2/C*.txt `metadata:` bytes:
  `byte2..7 = COLPF0,COLPF1,COLPF2,COLPF3,COLBK,COLB`. Index mapping (verified against the
  emulator): `pal0 = [COLB, COLPF0, COLPF1, COLPF2]`, `pal1 = [COLB, COLPF0, COLPF1, COLPF3]`
  (idx0 = the value-0 floor/background = COLB, NOT COLBK); `gr_atari_colbk[]` = COLBK for the
  `┼` black fill. The byte→RGB table (`tools/atari_pal_palette.txt`) was extracted once with a
  colour-chart ROM run under `atari800 -pal` (Xvfb + ffmpeg x11grab, sample band centres);
  RGB→BGR555 = `(r>>3)|((g>>3)<<5)|((b>>3)<<10)`.  NOTE: atari800's *generated* PAL palette
  is standard-atari800, which differs from Atari800MacX's default — we track standard atari800.
- **Bear initial facing must come from the Atari `STW*` routines, not the bird pattern.**
  `STW_DIR` in `convert_atari_levels.py` gives each creature's starting direction. Bears
  (ABCD left-hand `BEAR`, EFGH right-hand `BEAR_B`) face N,S,E,W / S,N,W,E; only the
  bird bouncers IJKL use `[2,3,0,1]`. A wall-follower that starts facing the wrong way
  hugs the wrong wall and walks out of its room (the level-3 bear "escapes the island"
  bug). The GNU `BEAR` left/right-hand logic is the *same* algorithm as the Atari's, so
  the authentic facing is all that's needed to match.
- **DZ1 guns = bullets (shottype 0), DZ2 = laser (shottype 1).** Getting shottype wrong
  makes projectiles render as the wrong object (e.g. laser beam looking like the cannon).
- After editing any header, `make clean` — header dependencies aren't fully tracked and you
  get stale-link errors otherwise.

## Sound

`play_sound` (glue.c) drops `SND_QUIET` events: the engine emits world sounds as
`SND_NORM` only when `in_viewport()`, and `render.c`'s `set_sound_viewport()` keeps
`in_viewport` aligned to the real GBC camera window. This kills the constant off-screen
gun/bird crackle. Robbo's own actions always pass `SND_NORM`.

## Performance (Level 51 — the slow one)

- Profiled ~22% render / ~78% logic. Render cost of barriers is wasted: barriers draw
  state-independently (no BARRIER case in `cell_glyph`), so re-uploading them every pulse
  is a no-op visually.
- **Shipped (+13%):** in `board_upd.c` `case BARRIER:`, detect a *solid* wall-to-wall run
  (no gaps) and skip the `move_object`/`negate_state` wave for it. Gapped fields (e.g. L50)
  fall through to the full wave and still animate. Verified: L51 0.11→0.12, L50 still
  rotates, all BehaviorTest assertions pass, no visual change.
- **Rejected/reverted, do not retry:** slowing the field march via `DELAY_BARRIER` (user:
  "too slow, restore the original velocity"); `move_object` pointer caching (zero effect);
  row-pointer table (worse on heavy levels); a `gr_barrier_done` guard.
- **Deferred (risky):** making solid barriers fully inert (`inlist=0`) at load time —
  risks making them indestructible. Not attempted.

## Manual test set

Minimum levels covering all tile/behaviour types ≥2× (excluding L12, which is bomb-rubble):
**11, 16, 27, 35, 45, 51, 54**. (L11 rotating gun, L16 teleports, L51 barrier field, etc.)

## Status / open threads

- Atari level conversion: complete and validated; default build.
- `[additional]` behaviour flags: now functional after the SDCC bitfield fix.
- L51 perf: the +13% safe win is shipped; no pending perf work.
- The emulator-level auto-solver (`Solver.java`) was a shelved experiment (0/56 — the level
  set is Sokoban-hard); it is not part of the product.
