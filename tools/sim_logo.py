#!/usr/bin/env python3
"""Faithfully reproduce the Atari TITLE.ASM BIGR routine to extract the authentic
'RoDDo' logo as a 2bpp colour-index bitmap (64x8 logical, value 0..3 per pixel:
0=background, 1=COLPF0 dark, 2=COLPF1 mid, 3=COLPF2 bright).

The Atari draws 5 glyphs (R,O,B,B,O) from I.FNT @ $3E8, doubling each 1bpp source
bit into a 2bpp 11/00 pixel, then ANDing a per-row-rotated diagonal MASK to carve
the 3D brick bevel.  We emulate the 6502 bit ops exactly."""
import sys

ORIG = None   # set by callers; defaults to argv[1]

def _build():
    orig = ORIG or sys.argv[1]
    d = open(orig + "/d2/I.FNT", "rb").read()[6:]   # strip 6-byte load header
    GBASE = 0x3E8
    ROBO = [0, 1, 2, 2, 1]                            # glyph index per letter R O B B O
    # MASK init (SETUP): MASK[0]=MASK+2=0xFA, MASK[1]=MASK+3=0x5A
    mask = [0xFA, 0x5A]

    def rol_mask():
        # exact port of ROLM: 16-bit rotate done via ASL/ROL/ROR/ROL, X=1..0 (2x)
        C = 0
        for _ in range(2):
            # ASL MASK+1
            C = (mask[1] >> 7) & 1; mask[1] = (mask[1] << 1) & 0xFF
            # ROL MASK
            nc = (mask[0] >> 7) & 1; mask[0] = ((mask[0] << 1) | C) & 0xFF; C = nc
            saved = C                       # PHP
            # ROR MASK+1
            nc = mask[1] & 1; mask[1] = ((C << 7) | (mask[1] >> 1)) & 0xFF; C = nc
            C = saved                       # PLP
            # ROL MASK+1
            nc = (mask[1] >> 7) & 1; mask[1] = ((mask[1] << 1) | C) & 0xFF; C = nc

    screen = [[0] * 16 for _ in range(8)]            # 16 bytes wide x 8 rows
    for lx in range(4, -1, -1):                      # BIG0: X = 4..0
        col = lx * 3 + 1                             # SCRA = SCRN+1 + lx*3
        addr = ROBO[lx] * 8
        for rx in range(7, -1, -1):                  # BIG1: X = 7..0
            row = 7 - rx                             # SCRA += 16 each iter -> rows 0..7
            hlp = d[GBASE + addr]
            for y in (0, 1):                         # two nibbles -> two bytes
                a = 0
                for _ in range(4):                   # BIG4: 4 source bits, doubled
                    c = (hlp >> 7) & 1; hlp = (hlp << 1) & 0xFF   # ASL HLP
                    a = ((a << 1) | c) & 0xFF        # ROL A
                    a = ((a << 1) | c) & 0xFF        # ROL A (PLP keeps same carry)
                a &= mask[y]                         # AND MASK,Y
                screen[row][col + y] = a
            rol_mask()                               # JSR ROLM (once per row)
            addr += 1

    # unpack to a 64x8 grid of 2-bit colour indices (mode E: MSB-first, 4px/byte)
    W, H = 64, 8
    grid = [[0] * W for _ in range(H)]
    for r in range(H):
        for bcol in range(16):
            b = screen[r][bcol]
            for p in range(4):
                v = (b >> (6 - p * 2)) & 3
                grid[r][bcol * 4 + p] = v

    # trim to the used column span so the logo is centred cleanly
    cols_used = [c for c in range(W) if any(grid[r][c] for r in range(H))]
    c0, c1 = min(cols_used), max(cols_used)
    return grid, c0, c1

def main():
    grid, c0, c1 = _build()
    print(f"used cols {c0}..{c1} (width {c1-c0+1})", file=sys.stderr)
    for r in range(8):
        print("".join(" .:#"[grid[r][c]] for c in range(c0, c1 + 1)), file=sys.stderr)

if __name__ == "__main__":
    main()

# ---- preview helpers (not used by the build pipeline) ----
def atari_rgb(c):
    """Approximate Atari NTSC colour byte -> (r,g,b)."""
    import math
    hue = (c >> 4) & 0xF
    lum = (c & 0xF) / 15.0
    if hue == 0:
        y = lum; return (int(y*255),)*3
    ang = math.radians((hue - 1) * 25.0 - 58.0)
    sat = 0.32
    Y = 0.2 + lum*0.8
    i = sat*math.cos(ang); q = sat*math.sin(ang)
    r = Y + 0.956*i + 0.621*q; g = Y - 0.272*i - 0.647*q; b = Y - 1.106*i + 1.703*q
    return tuple(max(0,min(255,int(v*255))) for v in (r,g,b))

def preview(outpath, scale_x=4, scale_y=6, hue_shift=0):
    from PIL import Image
    grid, c0, c1 = main_grid()
    W = c1 - c0 + 1; H = 8
    # COLPF0/1/2 init = 0x72,0x74,0x78 (hue 7). idx0=bg black.
    base = [0x00, 0x72, 0x74, 0x78]
    pal = []
    for c in base:
        if c == 0: pal.append((0,0,0)); continue
        h = ((((c>>4)&0xF) + hue_shift - 1) % 15) + 1
        pal.append(atari_rgb((h<<4) | (c & 0xF)))
    img = Image.new("RGB", (W*scale_x, H*scale_y), (0,0,0))
    px = img.load()
    for r in range(H):
        for c in range(W):
            col = pal[grid[r][c0+c]]
            for dy in range(scale_y):
                for dx in range(scale_x):
                    px[c*scale_x+dx, r*scale_y+dy] = col
    img.save(outpath)

def main_grid():
    return _build()
