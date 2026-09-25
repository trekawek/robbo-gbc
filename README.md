# Robbo — Game Boy Color

A Game Boy Color build of **Robbo**, the 1989 tile-based puzzle game (Janusz Pelc / LK Avalon),
built with [GBDK-2020](https://github.com/gbdk-2020/gbdk-2020).

It runs the game logic of **[GNU Robbo](https://sourceforge.net/projects/gnurobbo/)** — the
faithful open-source reimplementation — ported to the Game Boy Color, with the original Atari
8-bit graphics, sound and colours.

![Robbo on Game Boy Color — the rainbow title logo, then gameplay across several levels (blue walls, red brick walls, roaming guns).](docs/gameplay.gif)

## What it is

Robbo is a tile-based puzzle game. In each room you collect every **screw** to activate the exit
**capsule**, then reach it — while pushing boxes, shooting bullets, opening **doors** with keys,
using **teleports**, dodging **bombs**, **lasers** and **magnets**, and avoiding roaming
**monsters** (bears, birds, butterflies). The original extra-life glyphs give 200 score points
when collected. Touch a monster, a bomb blast or a wall you're forced into and the room
explodes; you respawn and try again.

- **Levels:** all 56 rooms converted from the original Atari level data.
- **Logic:** GNU Robbo's `board.c` engine — objects, monsters, explosions, teleports, magnets,
  guns/blasters, doors and the screw→capsule exit all behave exactly as upstream.
- **Look:** the original Atari **font** (16×16 metatiles from ANTIC mode-4 chars) and the
  authentic per-level **colour palettes**, plus the Atari **sound effects** and title
  **instruction text**.

## Controls

| Button | Action |
|--------|--------|
| D-pad | Move Robbo (walk, collect items, push boxes) |
| A + D-pad | Fire a bullet in that direction (uses ammo) |
| Hold B | Live overview: see the full room width and 16 rows at once; movement and firing still work |
| Start | Begin game (title screen) · in-game opens the pause menu |

The pause menu offers **Resume**, **Restart**, **Warp** (jump to any level),
and **Quit**. Build with `make OUTRO_MENU=1` to add **Outro** (watch the ending
and return to the current level) before Quit.
HUD (bottom two rows): screws left · keys · ammo · level. The pause menu shows your score.
Release B to return to the normal view. The overview uses compact 8×8 cells and follows
Robbo vertically, so you can watch distant hazards while timing a move or a shot.
Opening it takes about 0.1 seconds; once it appears, game timing matches the normal view.

## Build

Requires GBDK-2020 in `third_party/gbdk/` and `python3`. Initialize the Atari
source submodule before building:

```sh
git submodule update --init third_party/lkavalon-atari
make            # regenerates assets + levels, builds build/robbo.gbc
make clean
```

Asset conversion reads the Atari font, sound, text, levels and palettes from
`third_party/lkavalon-atari/robbo/`. Override its location with `ORIG=` if needed.

Output: `build/robbo.gbc` — a CGB ROM (MBC5) that runs in any Game Boy Color emulator or on
hardware via a flashcart.

## Reference notes

The [Atari outro comparison](docs/outro.md) documents the ending's timing and
sound cues. The [sound audit](docs/sound-audit.md) and [PAL colour audit](docs/pal-colors.md)
compare the GBC conversion with the original Atari game. [Performance measurements](docs/performance.md)
and [Atari PAL timing](docs/pal-timing.md) document the game clock and rendering work.

## Layout

```
src/board.c          GNU Robbo's engine (update_game split into upd_g1..g5 so SDCC compiles it)
src/board_upd.c      the per-object update cases (banked)
src/board_robbo.c    Robbo's own move/shoot logic (banked)
src/glue.c           main loop: input, tick pacing, render scheduling, pause menu
src/render.c         board -> tiles+palettes, the scrolling viewport, Robbo's facets
src/loader.c         level load: board setup + per-level palette + tile/HUD init
src/hud.c            window-layer HUD (screws/keys/ammo/level)
src/menu.c           title screen + pause/warp menu (banked)
src/atari_pal.c      the 56 authentic per-level Atari colour palettes (banked)
src/globals.c        shared game state
src/levels_*.c       generated level data (HOME index + banked grid modules)
src/object_tables.c  LOOK (byte->glyph) and animation tables
src/sound.c          GB-channel sound effects
src/gen/             generated assets (gfx_tiles.*, sounds.h, instr.h)
tools/               asset and level converters
```

### How rendering works

Each room is a grid of object bytes. An object is a **16×16 metatile** of four 8×8 chars:
`LOOK[b]` gives a screen code `sc`, and the four chars are `sc`, `sc|1`, `sc|0x20`, `sc|0x21`,
drawn from the Atari playfield charset (pixel-doubled to the chunky 8px look). A cell uses the
level's normal or inverse palette purely by the glyph's high (inverse) bit.

The playfield (16 cells wide × 31 tall = 256×496px) is larger than the screen, so a **scrolling
viewport follows Robbo** with fractional easing on every VBlank, independently of game-logic
speed. Both scroll registers update before the visible frame to avoid tearing. The BG map
is only 32×32 tiles, so the tall field can't fit at once: `render.c` keeps a **16-row rolling
window** in the map (`slot_owner[]`) and streams new rows ahead of the camera. Vertical
targets stay within the uploaded rows, including when following a teleport.
