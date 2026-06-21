# Robbo — Game Boy Color port

A port of the 1989 Atari 8-bit puzzle game **Robbo** (Janusz Pelc / LK Avalon) to the
Game Boy Color, built with [GBDK-2020](https://github.com/gbdk-2020/gbdk-2020).

Source game: https://github.com/trekawek/lkavalon-atari (the `robbo/` directory).

## What it is

Robbo is a tile-based puzzle game. In each room you collect every **screw** to activate the
exit **capsule**, then reach it — while pushing boxes, shooting bullets, using **keys/doors**
and **teleports**, and avoiding **bombs** and roaming **monsters**.

This port reuses the original's data faithfully: levels are the real C1 pack converted from the
source `.txt` files, graphics come from the original Atari fonts, and per-level colour palettes
are derived from each level's metadata. The engine keeps the original's **object byte codes**
(ATASCII) and renders via a port of the original `LOOK` table, so the screen matches the source.

## Controls

| Button | Action |
|--------|--------|
| D-pad | Move Robbo (walk, dig ground, push boxes, collect items) |
| A + D-pad | Fire a bullet in that direction (uses ammo) |
| Start | Begin game (title screen) |

HUD (bottom two rows): screws left · keys · ammo · lives · level · score.

## Build

Requires the bundled GBDK-2020 (downloaded to `third_party/gbdk/`) and `python3`.

```sh
make            # regenerates assets, builds build/robbo.gbc
make run        # launch in bgb (wine)
make clean
```

Output: `build/robbo.gbc` — a CGB ROM that runs in any Game Boy Color emulator or on hardware
via a flashcart.

### Verify headlessly

`tools/shot.py` boots the ROM in [PyBoy](https://github.com/Baekalfen/PyBoy) and saves a PNG:

```sh
python3 tools/shot.py build/robbo.gbc out.png 200 "120:start:5,200:up:40"
```

## Layout

```
tools/convert_font.py    Atari .FNT (ANTIC mode-4) -> GBC 2bpp tiles
tools/convert_levels.py  C1.txt -> C arrays (grid, screws, robbo start, BGR555 palettes)
tools/shot.py            headless screenshot helper (PyBoy)
src/object_tables.c      LOOK (byte->glyph) and NEXT (animation) tables, ported from R2.ASM
src/cave.c               the 16x31 object-byte grid
src/render.c             byte->tile+palette, vertical-scroll viewport, F.FNT base charset
src/player.c             movement, push, dig, collect, doors, teleport, fire
src/objects.c            bullets, monsters, bomb explosions
src/game.c               main loop, tick pacing, dirty-cell flush, level lifecycle
src/hud.c / title.c      window-layer HUD and title screen (I.FNT text + authored digits)
src/sound.c              GB-channel sound effects
src/gen/                 generated assets (gfx_tiles.*, levels_c1.*)
```

### How rendering works

The original stores each room as a grid of ATASCII object bytes. Each object is a **16×16
metatile** of four 8×8 ANTIC mode-4 chars: `LOOK[b]` gives a screen code `sc`, and the four chars
are `sc`, `sc|1` (top row), `sc|0x20`, `sc|0x21` (bottom row), drawn from the playfield charset
(base = **F.FNT**, loaded at `$1400` in the original). The high bit of `sc` selects the "inverse"
colour (a second palette).

Objects render at full **16×16** detail: each cave cell is a 2×2 block of four 8×8 GBC tiles
(the mode-4 4-colour-cells per char are pixel-doubled to 8px, preserving the chunky Atari look).
The playfield is 16 cells = 32 tiles = 256px wide and 31 cells = 62 tiles = 496px tall, larger
than the screen, so a **scrolling viewport follows Robbo** in both axes (smooth easing).

The BG map is only 32×32 tiles, so the 62-row-tall field can't fit at once: `render.c` keeps a
**16-cave-row rolling window** in the map (`slot_owner[]`, cave row → map row `(r&15)*2`) and
streams new rows in as the camera scrolls (`SCY` wraps every 256px = 16 rows). Horizontally all 16
cells fit the 32-wide map, so only `SCX` pans.

Walls (`0xA0`) render as glyph 0/1/0x20/0x21, taken per level-group from **M.FNT**. Robbo's facet
chars are blank in F.FNT (the original overlays them from **S.FNT** by facing direction), so they
are overlaid from S.FNT. Level colours come from each level's `CAPA` bytes: **COLB** is the
playfield background, COLPF0-3 the foreground colours, mapped to GBC BGR555.

## Status

Implemented (playable vertical slice): full engine, the 16-level C1 pack, screws/exit, ammo &
shooting, keys/doors, pushable boxes, bombs, teleports, roaming monsters, HUD, title, sound,
death/respawn, and level progression.

Follow-on (not in this slice): the C2/C3 level packs, exotic object behaviours (magnets, all
rotating-blaster variants, the `?`-capsule random table), two-frame charset animation, the demo
mode, and the congratulations sequence.
