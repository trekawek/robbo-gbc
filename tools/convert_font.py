#!/usr/bin/env python3
"""Convert Atari Robbo charsets (.FNT) to Game Boy Color 8x8 tiles.

Each Robbo object is a 16x16 METATILE made of four 8x8 ANTIC mode-4 chars:
  screen code `sc` -> chars  sc (top-left)  sc|1 (top-right)
                             sc|0x20 (bot-left)  sc|0x21 (bot-right)
A mode-4 char is 4 colour-cells wide (each shown 2px on Atari) x 8 scanlines.
We render at full 16x16: each char -> one 8x8 GBC tile with the colour-cells
pixel-doubled (4 -> 8 px), preserving the chunky Atari look. The renderer draws
each cell as a 2x2 block of these tiles and scrolls the viewport.

Outputs:
  font_tiles[128]   playfield charset (from F.FNT, the base font at $1400)
  robbo_chars[4]    robbo facet chars (S.FNT 8,9,40,41) -> overlay 0x5E,0x5F,0x7E,0x7F
  wall_chars[16*4]  per level-group wall chars (from M.FNT) -> overlay 0,1,0x20,0x21
  ifnt_tiles[64]    HUD text font (from I.FNT, Atari-internal order)
"""
import sys, os

def read_fnt(path):
    d = open(path, "rb").read()
    if d[0] == 0xFF and d[1] == 0xFF:
        d = d[6:]
    return [d[i*8:(i+1)*8] for i in range(len(d)//8)]

def char_tile(ch):
    """8x8 GBC 2bpp tile (16 bytes) from a mode-4 char, colour-cells doubled."""
    out = bytearray()
    for r in range(8):
        lo = hi = 0
        for c in range(4):
            v = (ch[r] >> (6 - c*2)) & 3
            for d in range(2):
                bit = 7 - (c*2 + d)
                if v & 1: lo |= 1 << bit
                if v & 2: hi |= 1 << bit
        out.append(lo); out.append(hi)
    return bytes(out)

def mono_tile(ch):
    """8x8 GBC 2bpp tile from a mode-2 (1bpp) char: each byte = 8 pixels,
       set bits -> colour index 3.  Used for the I.FNT status/HUD font."""
    out = bytearray()
    for r in range(8):
        out.append(ch[r]); out.append(ch[r])   # both planes -> value 3
    return bytes(out)

def logo_3d(ch, scale=3):
    """8x8 1bpp char -> a scale*scale grid of 8x8 GBC tiles forming a big 3D
       blocky letter: body = colour 2, top/left highlight = 3, bottom/right
       shadow = 1.  Returns the tiles row-major (top row first)."""
    n = 8 * scale
    body = [[0]*(n+2) for _ in range(n+2)]            # 1px padding for edge tests
    for r in range(8):
        for c in range(8):
            if (ch[r] >> (7 - c)) & 1:
                for dr in range(scale):
                    for dc in range(scale):
                        body[1 + r*scale + dr][1 + c*scale + dc] = 1
    col = [[0]*n for _ in range(n)]
    for r in range(n):
        for c in range(n):
            if body[r+1][c+1]:
                if not body[r][c+1] or not body[r+1][c]:      v = 3   # top/left
                elif not body[r+2][c+1] or not body[r+1][c+2]: v = 1  # bottom/right
                else:                                          v = 2   # body
                col[r][c] = v
    tiles = []
    for ty in range(scale):
        for tx in range(scale):
            t = bytearray()
            for r in range(8):
                lo = hi = 0
                for c in range(8):
                    v = col[ty*8 + r][tx*8 + c]
                    bit = 7 - c
                    if v & 1: lo |= 1 << bit
                    if v & 2: hi |= 1 << bit
                t.append(lo); t.append(hi)
            tiles.append(bytes(t))
    return tiles                                      # scale*scale tiles

def emit(out, name, tiles):
    flat = b"".join(tiles)
    out.write(f"const unsigned char {name}[{len(flat)}] = {{\n")
    for i in range(0, len(flat), 16):
        out.write("  " + ",".join("0x%02X" % b for b in flat[i:i+16]) + ",\n")
    out.write("};\n\n")

def main():
    orig, outdir = sys.argv[1], sys.argv[2]
    d2 = os.path.join(orig, "d2")
    f = read_fnt(os.path.join(d2, "F.FNT"))   # base playfield charset (128)
    s = read_fnt(os.path.join(d2, "S.FNT"))   # animation source incl. robbo (128)
    m = read_fnt(os.path.join(d2, "M.FNT"))   # wall shapes (64)
    i = read_fnt(os.path.join(d2, "I.FNT"))   # HUD font (128)

    # Base playfield font is F.FNT, but the animated glyph ranges are BLANK in
    # F.FNT - the original scatters them in from S.FNT (CHNMON). Reproduce that:
    #   FONT 10-21 <- S.FNT 16-27 ;  FONT 42-53 <- S.FNT 48-59  (frame 0, +6)
    #   FONT 28-31 <- S.FNT 28-31 ;  FONT 60-63 <- S.FNT 60-63  (frame 0, +0)
    # Frame 1 of each is the same source +64 chars (the S.FNT $1200 bank).
    def font_src(c):
        if 10 <= c <= 21 or 42 <= c <= 53: return s[c + 6]
        if 28 <= c <= 31 or 60 <= c <= 63: return s[c]
        return f[c]
    font = [char_tile(font_src(c)) for c in range(128)]

    # robbo directional facets, in gs.facing order (0=up 1=down 2=left 3=right).
    # The original FACET routine (R1.ASM) maps the joystick (UP=bit0,DOWN=1,
    # LEFT=2,RIGHT=3) to S.FNT char pairs: RIGHT=8,9/40,41  LEFT=10,11/42,43
    # DOWN=12,13/44,45  UP=14,15/46,47.  Each facet = TL,TR (top) then BL,BR.
    ROBBO_FACETS = [
        (14, 15, 46, 47),   # 0 up    (back of head)
        (12, 13, 44, 45),   # 1 down  (eyes toward viewer)
        (10, 11, 42, 43),   # 2 left
        ( 8,  9, 40, 41),   # 3 right
    ]
    # two walk frames per direction (frame 1 = +64 chars, the S.FNT $1200 bank),
    # so the renderer animates Robbo's stride as he steps cell to cell.  Layout:
    # facet (dir*2 + frame), each = 4 tiles TL,TR,BL,BR.
    robbo = []
    for tl, tr, bl, br in ROBBO_FACETS:
        for fr in (0, 64):
            robbo += [char_tile(s[tl + fr]), char_tile(s[tr + fr]),
                      char_tile(s[bl + fr]), char_tile(s[br + fr])]

    # animated glyph chars.  The original CHNMON (R1.ASM) re-copies FONT chars
    # 10-21 (+42-53 in the inverse half) and 28-31 (+60-63) from S.FNT every 2
    # ticks, alternating the source bank - that's the playfield shimmer.  We must
    # animate the SAME range: chars 10-13/42-45 carry the laser beam segments
    # (']' '['), cannon facets, etc., so omitting them left the beams static.
    # frame 0 source matches the base font; frame 1 is +64 chars (S.FNT $1200 bank).
    def asrc0(c):
        return (c + 6) if (10 <= c <= 21 or 42 <= c <= 53) else c
    ANIM = list(range(10, 22)) + list(range(42, 54)) + list(range(28, 32)) + list(range(60, 64))
    anim_a = [char_tile(s[asrc0(c)]) for c in ANIM]
    anim_b = [char_tile(s[asrc0(c) + 64]) for c in ANIM]
    walls = []
    for g in range(16):
        for c in (g*2, g*2+1, g*2+32, g*2+33):          # chars 0,1,0x20,0x21 per group
            walls.append(char_tile(m[c]))
    hud = [mono_tile(i[c]) for c in range(96)]           # text + status icons (mode-2)

    # title logo: 3x-scaled 3D letters R,O,B from I.FNT (R=0x32 O=0x2F B=0x22)
    logo = logo_3d(i[0x32]) + logo_3d(i[0x2F]) + logo_3d(i[0x22])  # 3 letters x 9 = 27

    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "gfx_tiles.h"), "w") as h:
        h.write("#ifndef GFX_TILES_H\n#define GFX_TILES_H\n")
        h.write("#include <gb/gb.h>\n")
        h.write("#define FONT_NTILES 128\n")
        h.write("#define WALL_NGROUPS 16\n")
        h.write("#define ANIM_NCHARS %d\n" % len(ANIM))
        h.write("extern const unsigned char font_tiles[];\n")
        h.write("extern const unsigned char robbo_chars[];\n")
        h.write("extern const unsigned char wall_chars[];\n")
        h.write("extern const unsigned char ifnt_tiles[];\n")
        h.write("extern const unsigned char anim_a[];\n")
        h.write("extern const unsigned char anim_b[];\n")
        h.write("extern const unsigned char anim_slots[];\n")
        h.write("extern const unsigned char logo_tiles[];\n")
        h.write("BANKREF_EXTERN(gfx)\n")
        h.write("#define GFX_BANK BANK(gfx)   /* SWITCH_ROM(GFX_BANK) before reading the tiles */\n")
        h.write("#endif\n")
    with open(os.path.join(outdir, "gfx_tiles.c"), "w") as c:
        c.write("#pragma bank 255\n")          # auto-assigned to a free ROM bank
        c.write('#include "gfx_tiles.h"\n\n')
        emit(c, "font_tiles", font)
        emit(c, "robbo_chars", robbo)
        emit(c, "wall_chars", walls)
        emit(c, "ifnt_tiles", hud)
        emit(c, "anim_a", anim_a)
        emit(c, "anim_b", anim_b)
        c.write("const unsigned char anim_slots[%d] = {" % len(ANIM)
                + ",".join("0x%02X" % v for v in ANIM) + "};\n")
        emit(c, "logo_tiles", logo)
        c.write("BANKREF(gfx)\n")               # bank-number symbol for GFX_BANK
    print(f"gfx: font={len(font)} robbo={len(robbo)//4}facets walls={len(walls)} hud={len(hud)} -> {outdir}/gfx_tiles.[ch]")

if __name__ == "__main__":
    main()
