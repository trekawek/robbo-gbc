#!/usr/bin/env python3
"""Convert a Robbo level pack (.txt) to GBC C arrays.

Each level in the .txt is 31 rows x 16 glyphs + a `metadata:` hex line (16 bytes).
Glyphs map to ATASCII object bytes via the same table as util/level-parser.go.
These ATASCII bytes ARE the object codes the engine dispatches/renders on.

Per-level metadata (CAPA, 16 bytes):
  [1] screw count (ZEBR)
  [2..6] Atari color regs COLPF0..3, COLBK  (-> 708..712)
  [7] border/background color COLB (-> 707)

We emit, per level: the 16x31 grid, screw count, robbo start (the '*' cell),
and two 4-colour GBC BG palettes (BGR555): normal + inverse (PF3) variant.
"""
import sys, os, colorsys

ATASCII = {
    '♥': 0x00, '├': 0x01, '┤': 0x04, '┐': 0x05,
    '╱': 0x06, '◣': 0x0A, '▔': 0x0D, '▂': 0x0E,
    '▖': 0x0F, '┌': 0x11, '─': 0x12, '┼': 0x13,
    '•': 0x14, '┬': 0x17, '┴': 0x18, '↑': 0x1C,
    '↓': 0x1D, '←': 0x1E, '→': 0x1F, '█': 0xA0,
}
WIDTH, HEIGHT = 16, 31

def to_byte(ch):
    if ch in ATASCII:
        return ATASCII[ch]
    return ord(ch) & 0xFF

def atari_to_bgr555(c):
    """Atari (hue<<4 | luma) -> GBC BGR555.
    The Atari colour wheel runs clockwise from gold (hue 1). Calibrated against
    real PAL output so hue1->gold, hue7->blue, hueB->green (the level-1 colours).
    Greys (hue 0) follow the PAL luminance ramp via a gamma curve - a raw linear
    lum is far too dark in the midtones (e.g. the COLB=$04 floor on planet 10)."""
    hue = (c >> 4) & 0xF
    lum = (c & 0xF) / 15.0
    if hue == 0:
        r = g = b = lum ** 0.55
    else:
        ang = (40.0 - (hue - 1) * 28.0) % 360.0
        sat = 0.80
        val = 0.42 + 0.58 * lum
        r, g, b = colorsys.hsv_to_rgb(ang / 360.0, sat, val)
    def c5(x):
        return max(0, min(31, int(round(max(0.0, min(1.0, x)) * 31))))
    return (c5(b) << 10) | (c5(g) << 5) | c5(r)

def parse(path):
    lines = [l.rstrip('\n') for l in open(path, encoding='utf-8')]
    levels = []
    i = 0
    while i < len(lines):
        if lines[i].strip() == "### level ###":
            rows = lines[i+1:i+1+HEIGHT]
            meta_line = lines[i+1+HEIGHT]
            meta = bytes.fromhex(meta_line[len("metadata: "):])
            grid = []
            rx = ry = 0
            for y, row in enumerate(rows):
                cells = [to_byte(ch) for ch in row]
                cells = (cells + [0x20]*WIDTH)[:WIDTH]
                if 0x2A in cells:           # '*' = robbo start
                    rx, ry = cells.index(0x2A), y
                grid.append(cells)
            levels.append((grid, meta, rx, ry))
            i += 2 + HEIGHT
        else:
            i += 1
    return levels

def main():
    src, outdir, name = sys.argv[1], sys.argv[2], sys.argv[3]
    levels = parse(src)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, f"levels_{name}.h"), "w") as h:
        h.write(f"#ifndef LEVELS_{name.upper()}_H\n#define LEVELS_{name.upper()}_H\n")
        h.write("#include <gb/gb.h>\n")
        h.write("#include \"../level.h\"\n")
        h.write(f"#define {name.upper()}_NLEVELS {len(levels)}\n")
        h.write(f"extern const Level {name}_levels[{len(levels)}];\n")
        h.write(f"BANKREF_EXTERN({name}_levels)\n")
        h.write("#endif\n")
    with open(os.path.join(outdir, f"levels_{name}.c"), "w") as c:
        c.write("#pragma bank 255\n")            # auto-assigned to a free ROM bank
        c.write(f'#include "levels_{name}.h"\n\n')
        for idx, (grid, meta, rx, ry) in enumerate(levels):
            flat = [b for row in grid for b in row]
            c.write(f"static const unsigned char {name}_grid{idx}[{WIDTH*HEIGHT}] = {{\n")
            for y in range(HEIGHT):
                row = ",".join(f"0x{b:02X}" for b in flat[y*WIDTH:(y+1)*WIDTH])
                c.write(f"  {row},\n")
            c.write("};\n")
        c.write(f"\nconst Level {name}_levels[{len(levels)}] = {{\n")
        for idx, (grid, meta, rx, ry) in enumerate(levels):
            screws = (meta[1] >> 4) * 10 + (meta[1] & 0x0F)   # CAPA+1 is BCD
            # COLB (meta[7]) is the visible playfield background colour.
            # pixel 0 -> background, 1 -> COLPF0, 2 -> COLPF1, 3 -> COLPF2.
            bg = atari_to_bgr555(meta[7])
            pal_n = [bg, atari_to_bgr555(meta[2]),
                     atari_to_bgr555(meta[3]), atari_to_bgr555(meta[4])]
            # inverse tiles (walls etc.): pixel 3 -> COLPF3
            pal_i = [bg, pal_n[1], pal_n[2], atari_to_bgr555(meta[5])]
            palstr = ",".join(f"0x{v:04X}" for v in (pal_n + pal_i))
            c.write(f"  {{ {name}_grid{idx}, {screws}, {rx}, {ry}, {{ {palstr} }} }},\n")
        c.write("};\n")
        c.write(f"BANKREF({name}_levels)\n")     # bank-number symbol for BANK()
    print(f"levels {name}: {len(levels)} -> {outdir}/levels_{name}.[ch]")

if __name__ == "__main__":
    main()
