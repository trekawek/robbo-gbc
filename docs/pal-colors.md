# Atari PAL colour audit

The RGB reference is **Altirra 4.10's Default PAL** preset, with the XL/XE luma
map, no colour matching, gamma 1.00 and PAL artifacting disabled. The original
`bin/robbo.xex` was run in Altirra under Wine. All 56 rooms were selected through
the game's own setup routine and captured. In every room, the five expected
playfield colours and both HUD colours from Altirra's palette export appear
exactly in the rendered pixels: **392 of 392 checks**.

The level colour-register bytes still come from the original `d2/C1.txt`,
`C2.txt` and `C3.txt` metadata. The port previously converted those bytes with
Atari800 5.2.0's standard PAL palette. It now uses Altirra's exported RGB
table for the playfield, HUD and title.

## Corrections

| Element | Atari rule and GBC correction | Coverage |
|---|---|---|
| Normal glyphs | `[COLB, COLPF0, COLPF1, COLPF2]` | All 56 levels |
| Inverse glyphs | `[COLB, COLPF0, COLPF1, COLPF3]` | All 56 levels |
| Solid cave fill (`┼`) | `LOOK` maps to glyph `$42`, whose pixels are all value 1: use **COLPF0**, previously COLBK | Visible colour changes in levels 4, 15, 35, 40, 42, 54 |
| Plain birds (`I`–`L`) | Normal palette, previously inverse | 79 birds across 26 levels |
| Moving guns (`$0D/$0E`) | Inverse palette, previously normal | 8 guns in levels 45, 46, 47, 48, 50, 51 |
| Normal walls (`\`) | Normal palette with wall glyph `$00`, previously inverse | 139 source cells |
| Sliding crates (`╱`) | Normal palette with glyph `$4E`, both stationary and moving; loaded as push boxes | 22 crates across levels 29, 30, 31, 33, 39 and 42 |
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
The committed 256-entry text table records those aliases explicitly. Altirra's
raw 768-byte export is kept as `tools/altirra_default_pal.pal`. GBC channels
have five bits, so conversion uses `round(channel * 31 / 255)` and packs the
result as BGR555. This minimizes channel error for full-range RGB555 expansion.
All 56 level palettes change from the previous Atari800-based table.

An Atari television and a physical GBC LCD have different colour responses.
Emulator colour-correction profiles also differ. The target here is Altirra's
Default PAL palette represented in nominal full-range RGB555. Coffee GB's
uncorrected screenshots expand channels by multiplying by 8, and its default
corrected display uses a different response curve; neither is an exact full-range
RGB555 preview. Compare palette registers and colour placement as well as PNGs.
The palette export bypasses X11 display scaling. The room captures were made
from an Xvfb window and used only to confirm that the exported RGB values
appear in Robbo's own rendered playfield and HUD.

## Reproduce the reference

Run the original executable in Altirra on PAL timing with artifacting disabled:

```sh
wine64 /opt/altirra/Altirra64.exe /w /pal /artifact:none \
  /run "$(winepath -w "$ORIG/bin/robbo.xex")"
```

In **View > Adjust Colors**, select **Default PAL**, **XL/XE** luma map,
**None** for colour matching and gamma **1.00**. Use **File > Export Palette**
to save `tools/altirra_default_pal.pal` as an Atari800 palette file (768 bytes).
`tools/convert_altirra_palette.py` converts the export into the GTIA lookup
table, aliasing odd colour values to the preceding even value.

To inspect a specific original room, press F8 to open Altirra's debugger,
enter `a8` for Atari800-compatible commands, then use `c 9d NN`,
`setpc 1d22`, `sets fd`, `cont` to enter room `NN+1` through Robbo's original
`SETU` routine. The addresses are `CNUM` and `SETU` from the matching
`d1/R1.ASM`; `NN` is the hexadecimal value of the zero-based level number
(for example, `33` selects level 52). Check the addresses against an assembled
label table if the executable changes. Let the room draw before pausing with F8
and capturing it. All 56 rooms were checked this way against the exported palette.

The earlier Atari800-only capture tool, `tools/atari_palette_reference.py`,
remains available for comparing emulator presets.

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
