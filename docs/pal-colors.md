# Atari PAL colour audit

All 56 rooms were compared with the original `bin/robbo.xex` running in
**Atari800 5.2.0**, using a fresh configuration, PAL mode, the standard colour
preset and no PAL artifact filter. All **336 live level colour registers**
match the original `d2/C1.txt`, `C2.txt` and `C3.txt` metadata. Native indexed PNG
captures also confirm the two status-line colours in every room.

The existing playfield RGB reference was already correct. The main differences
were where the port applied those colours: cave fill, some object variants,
normal walls and the status line. The reference table now includes every Atari
colour so the HUD and title use the same PAL conversion as the playfield.

## Corrections

| Element | Atari rule and GBC correction | Coverage |
|---|---|---|
| Normal glyphs | `[COLB, COLPF0, COLPF1, COLPF2]` | All 56 levels |
| Inverse glyphs | `[COLB, COLPF0, COLPF1, COLPF3]` | All 56 levels |
| Solid cave fill (`┼`) | `LOOK` maps to glyph `$42`, whose pixels are all value 1: use **COLPF0**, previously COLBK | Visible colour changes in levels 4, 15, 35, 40, 42, 54 |
| Plain birds (`I`–`L`) | Normal palette, previously inverse | 79 birds across 26 levels |
| Moving guns (`$0D/$0E`) | Inverse palette, previously normal | 8 guns in levels 45, 46, 47, 48, 50, 51 |
| Normal walls (`\`) | Normal palette with wall glyph `$00`, previously inverse | 139 source cells |
| Diagonal source character (`╱`) | Normal palette with glyph `$4E`, previously an inverse wall glyph | 22 source cells; existing wall collision behavior retained |
| Status line | COLBK background; foreground uses COLBK's hue with luminance `$A`, previously fixed white on black | All 56 levels |
| Exit opening | `WYZA` briefly flashes COLB to `$0F`; restore the level floor afterward | Previously missing; only normal/inverse entry 0 changes |
| Title rainbow | Standard PAL reference table, replacing the separate approximate NTSC conversion | All 15 animated hues |

Plain-bird corrections apply to levels 3, 4, 6, 7, 9, 13, 14, 16, 19, 22, 25,
26, 28, 29, 31, 34, 35, 36, 39, 42, 44, 47, 49, 52, 53 and 56.

The status-line rule comes from the original `DLI2`: ANTIC mode 2 uses COLPF2
for the background and combines its hue with COLPF1's luminance for text.
The game supplies COLBK and `$0A` respectively. For example, level 16 uses
`$D0/$DA`, rather than black/white. The cave-fill distinction is especially
visible in level 4, where COLPF0 is dark blue (`$70`) and COLBK is black (`$00`).

The flash lasts three PAL frames (60.17 ms). Four GBC frames (66.97 ms) are the
nearest whole-frame duration. Writes occur in the existing HOME VBlank handler;
the HUD and solid cave-fill palettes are unaffected. Loading resets the flash.

## Colour precision and displays

GTIA ignores colour-register bit 0: odd values alias their even neighbours.
The committed 256-entry table records those aliases explicitly. GBC channels
have five bits, so conversion uses `round(channel * 31 / 255)` and packs the
result as BGR555. This minimizes channel error for full-range RGB555 expansion.
It changes the encoded palette in 49 of the 56 rooms, usually by one channel step.

An Atari television and a physical GBC LCD have different colour responses.
Emulator colour-correction profiles also differ. The target here is Atari800's
standard PAL palette represented in nominal full-range RGB555. Coffee GB's
uncorrected screenshots expand channels by multiplying by 8, and its default
corrected display uses a different response curve; neither is an exact full-range
RGB555 preview. Compare palette registers and colour placement as well as PNGs.
The Atari captures bypass X11 recording, video conversion and display profiles.

## Reproduce the reference

Install Atari800, MADS, Xvfb, and Python packages `python-xlib`, `pexpect`, and
`Pillow`. Use the matching original Atari executable and assembler source:

```sh
python3 tools/atari_palette_reference.py "$ORIG" build/atari-pal-reference
```

The tool resolves symbols from `R1.ASM`, selects all 56 rooms through their
original initialization routine, validates metadata and live colour registers,
and verifies HUD pixel indices. It writes native `level-01.png` through
`level-56.png`, `palettes.csv`, `reference.json`, and the full RGB palette.
`--display :94` can select another X display if the default is occupied.

`make` regenerates `src/atari_pal.c`, the title assets and converted level data
when their relevant palette/source inputs change.

## GBC verification

Use a built Coffee GB core and its dependency JARs on `COLOR_TEST_CP`. Generate
matching ROM/linker symbols, including `build/render.sym`:

```sh
make clean
make -j3 LCCFLAGS_EXTRA='-Wl-m -Wl-j'
java --class-path "$COLOR_TEST_CP" tools/ColorTest.java \
  build/robbo.gbc build/robbo.noi "$ORIG" \
  tools/atari_pal_palette.txt build/color-captures
```

The regression reads original metadata and `R2.ASM`'s `LOOK` table independently
of the asset converters. It checks every room's normal, inverse, fill and HUD
palettes, then freezes actors and scrolls through each entire map to check all
four tile attributes of every source cell. It also checks that the exit flash
changes only the two COLB entries and restores them after exactly four VBlanks.
Native GBC screenshots, a 56-room contact sheet and a mismatch report are saved
alongside the results. Optional `legacy` mode compares a previous build using
its old palette table and reports the original attribute mismatches.

The corrected build passed all **27,776 source cells / 111,104 tile attributes**
with zero palette-assignment differences. All 56 level/HUD palette checks and
the four-frame flash test passed. PAL timing and camera regressions also passed;
held movement averaged 140.644 ms/cell against the 140.391 ms PAL reference.

The PAL timing and camera regression commands remain in
[`pal-timing.md`](pal-timing.md) and the [README](../README.md).
