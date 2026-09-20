#!/usr/bin/env python3
"""Convert authentic Atari Robbo levels (lkavalon-atari d2/C*.txt) into the
gnu-robbo .dat format the GBC port's loader consumes, so the GBC shows the real
Atari level designs instead of gnu-robbo's re-creations.

Atari levels are 16x31 grids of ATASCII object bytes (semantics from the Atari
engine, ported in robbo-gbc commit 882ea08 src/objects.c).  We map each byte to
the equivalent gnu-robbo .dat glyph + [additional] params (directions, teleport
groups, gun shot-types), per src/loader.c transform_char + the [additional]
switch.

Output: a merged original.dat = Atari levels for the requested range, gnu-robbo
levels for the rest (so convert_gnu_levels.py's SKIP={53,58} still yields 56).

Usage: convert_atari_levels.py ATARI_D2_DIR GNU_ORIGINAL_DAT OUT_DAT [N_ATARI]
  N_ATARI = how many leading levels to take from Atari (default 3, for L1-3).
"""
import sys, re

ATARI_DIR, GNU_DAT, OUT_DAT = sys.argv[1], sys.argv[2], sys.argv[3]
N_ATARI = int(sys.argv[4]) if len(sys.argv) > 4 else 3
W, H = 16, 31

# d2 glyph -> ATASCII byte (level-parser.go ATASCII_TO_ASCII, inverted)
ATASCII = {0x00:'♥',0x01:'├',0x04:'┤',0x05:'┐',0x06:'╱',0x0A:'◣',0x0D:'▔',
 0x0E:'▂',0x0F:'▖',0x11:'┌',0x12:'─',0x13:'┼',0x14:'•',0x17:'┬',0x18:'┴',
 0x1C:'↑',0x1D:'↓',0x1E:'←',0x1F:'→',0xA0:'█'}
G2B = {v: k for k, v in ATASCII.items()}
def to_byte(ch): return G2B[ch] if ch in G2B else (ord(ch) & 0xFF)

# old-engine dir (0=up,1=down,2=left,3=right) -> gnu dir (0=E,1=S,2=W,3=N)
OLD2GNU = {0: 3, 1: 1, 2: 2, 3: 0}
# STW creature initial facing (old dir), derived from the Atari STWA..STWL
# routines (d1/R2.ASM): each tries (turn-toward-wall, forward, rotate) in order,
# so the 2nd probe is "forward" = the facing.  Left-hand bears ABCD face N,S,E,W;
# right-hand bears EFGH face S,N,W,E; bird bouncers IJKL travel W,E,up,down.
# The bears must NOT reuse the bird pattern or a left-hand follower starts facing
# the wrong way and wall-hugs out of its room (the level-3 bear bug).
STW_DIR = [0,1,3,2,  1,0,2,3,  2,3,0,1]

class Cell:
    __slots__ = ("ch", "add")  # gnu glyph; add = list of param ints (or None)
    def __init__(self, ch, add=None): self.ch = ch; self.add = add

def byte_to_cell(b):
    if b == 0xA0: return Cell('O')                     # solid (visible) wall
    if b == 0x13: return Cell('-')                     # black inner-cave fill -> BLACK_WALL
                                                       # (glyph $42 is solid COLPF0)
    if b == 0x20: return Cell('.')                     # empty
    if b == 0x2A: return Cell('R')                     # robbo start
    if b == 0x21: return Cell("'")                     # ammo
    if b == 0x23: return Cell('#')                     # box
    if b == 0x24: return Cell('T')                     # screw
    if b == 0x25: return Cell('H')                     # ground
    if b == 0x3D: return Cell('%')                     # key
    if b in (0x12, 0x7C): return Cell('D')             # door (h / v)
    if b == 0x40: return Cell('b')                     # bomb
    if b == 0x3F: return Cell('?')                     # questionmark
    if b == 0x14: return Cell('!')                     # capsule
    if b == 0x2B: return Cell('.')                     # extra life (unsupported)
    if b == 0x26: return Cell('V')                     # chaser -> butterfly
    if 0x30 <= b <= 0x39:                              # teleport: Atari digit group is
        return Cell('&', [b - 0x30 + 1, 0])            # 0-based, gnu group is 1-BASED
                                                       # (group 0 => teleportnumber 0 =
                                                       # disabled); id filled in later
    if 0x41 <= b <= 0x4C:                              # STW creatures
        gdir = OLD2GNU[STW_DIR[b - 0x41]]
        if 0x49 <= b <= 0x4C:                          # IJKL = bouncers -> bird
            return Cell('^', [gdir, 0, 0])             # bounces on dir axis, no shoot
        ch = '*' if 0x45 <= b <= 0x48 else '@'         # ABCD bear / EFGH black bear
        return Cell(ch, [gdir])
    if b in (0x4D, 0x4E):                              # bats: move horiz + shoot down
        gdir = 2 if b == 0x4D else 0                   # M=left(W), N=right(E)
        return Cell('^', [gdir, 1, 1])                 # dir2=S (shoot down), shooting
    if b in (0x28, 0x29):                              # magnet
        return Cell('M', [0 if b == 0x28 else 2])
    if b in (0x1C, 0x1D, 0x1E, 0x1F):                  # DZ1 cannon: fires BULLETS
        gdir = OLD2GNU[{0x1C:0,0x1D:1,0x1E:2,0x1F:3}[b]]
        return Cell('}', [gdir, gdir, 0, 0, 0, 0])     # shottype 0 = normal bullets
    if b in (0x5E, 0x22, 0x3C, 0x3E):                  # DZ2 laser cannon -> solid gun
        gdir = OLD2GNU[{0x5E:0,0x22:1,0x3C:2,0x3E:3}[b]]
        return Cell('}', [gdir, gdir, 1, 0, 0, 0])     # shottype 1 = laser beam
    if b in (0x01, 0x04, 0x17, 0x18):                  # DZ3 bullet gun
        gdir = OLD2GNU[{0x18:0,0x17:1,0x01:2,0x04:3}[b]]
        return Cell('}', [gdir, gdir, 0, 0, 0, 0])
    if 0x2C <= b <= 0x2F:                              # DZRU rotating cannon ,-./
        gdir = OLD2GNU[{0x2C:3,0x2D:1,0x2E:2,0x2F:0}[b]]
        return Cell('}', [gdir, gdir, 0, 0, 1, 0])     # rotable gun
    if b in (0x0D, 0x0E):                              # MOD moving cannon (fires up)
        d2 = 2 if b == 0x0D else 0                     # crawls W (0x0D) / E (0x0E)
        return Cell('}', [3, d2, 0, 1, 0, 0])          # fire N, movable
    if b in (0x0F, 0x11): return Cell('=', [0])        # ZAPO force field -> barrier row
    if b == 0x5C: return Cell('O', [9])               # normal-palette wall glyph $00
    if b == 0x06: return Cell('O', [10])              # normal-palette glyph $4E
    if b == 0x05: return Cell('O')                    # inverse wall (ZAPEND)
    if b in (0x5B, 0x5D): return Cell('.')             # transient laser beam segment
    return Cell('.')                                   # unknown -> empty (logged)

def parse_atari(d2dir):
    levels = []
    for f in ("C1", "C2", "C3"):
        lines = [l.rstrip("\n") for l in open(f"{d2dir}/{f}.txt", encoding="utf-8")]
        i = 0
        while i < len(lines):
            if lines[i].strip() == "### level ###":
                levels.append(lines[i+1:i+1+H]); i += 1 + H
            else: i += 1
    return levels

def atari_level_to_dat(rows, num):
    grid = [[Cell('.') for _ in range(W)] for _ in range(H)]
    unknown = {}
    for y in range(H):
        row = rows[y] if y < len(rows) else ""
        for x in range(W):
            ch = row[x] if x < len(row) else ' '
            b = to_byte(ch)
            c = byte_to_cell(b)
            if c.ch == '.' and b not in (0x20, 0x2B):
                unknown[ch] = unknown.get(ch, 0) + 1
            grid[y][x] = c
    # assign teleport ids per group in scan order
    tele_count = {}
    for y in range(H):
        for x in range(W):
            c = grid[y][x]
            if c.ch == '&':
                g = c.add[0]
                c.add[1] = tele_count.get(g, 0)
                tele_count[g] = c.add[1] + 1
    # build [data] + [additional]
    data = "".join("".join(c.ch for c in grid[y]) for y in range(H))
    data_rows = [ "".join(c.ch for c in grid[y]) for y in range(H) ]
    add = []
    for y in range(H):
        for x in range(W):
            c = grid[y][x]
            if c.add is not None and c.ch != '.':
                add.append((x, y, c.ch, c.add))
    if unknown:
        sys.stderr.write(f"L{num}: unmapped glyphs {unknown}\n")
    return data_rows, add

def parse_gnu(dat):
    """Return ordered list of dicts {num,colour,grid(list rows),add(list)} for all
    gnu levels, preserving their raw text (we only replace 1..N_ATARI)."""
    text = [l.rstrip('\r\n') for l in open(dat, encoding='latin-1')]
    levels = {}
    i = 0; cur = None; mode = None
    while i < len(text):
        line = text[i]
        if line == '[level]':
            cur = {'num': int(text[i+1]), 'colour': None, 'grid': [], 'add': []}
            levels[cur['num']] = cur; i += 2; mode = None; continue
        if line == '[colour]': cur['colour'] = text[i+1]; i += 2; mode = None; continue
        if line == '[size]': i += 2; mode = None; continue
        if line in ('[author]',): i += 2; mode = None; continue
        if line == '[level_notes]': i += 1; mode = 'skip'; continue
        if line == '[data]': mode = 'data'; i += 1; continue
        if line == '[additional]':
            mode = None; i += 1; cnt = int(text[i]); i += 1
            for _ in range(cnt):
                cur['add'].append(text[i]); i += 1
            continue
        if line == '[end]': mode = None; i += 1; continue
        if mode == 'data':
            if line == '' or line.startswith('['): mode = None; continue
            cur['grid'].append(line); i += 1; continue
        if mode == 'skip':
            if line.startswith('['): mode = None; continue
            i += 1; continue
        i += 1
    return levels

def emit_level(out, num, rows, add, colour):
    out += ["[level]", str(num), "[colour]", colour, "[size]", f"{W}.{H}", "[data]"]
    out += rows
    out += ["[additional]", str(len(add))]
    for (x, y, ch, vals) in add:
        out.append(".".join([str(x), str(y), ch] + [str(v) for v in vals]))
    out += ["[end]"]

def main():
    atari = parse_atari(ATARI_DIR)        # 56 authentic Atari levels (game order)
    gnu = parse_gnu(GNU_DAT)              # used only for per-level colour
    NA = len(atari)
    if N_ATARI >= NA:
        # FULL: emit exactly the 56 Atari levels 1:1 (no gnu fill, no SKIP needed;
        # convert_gnu_levels.py skips nothing when last_level == 56).
        out = ["[name]", "AtariRobbo", "[last_level]", str(NA)]
        for num in range(1, NA + 1):
            rows, add = atari_level_to_dat(atari[num-1], num)
            colour = (gnu.get(num, {}).get('colour')) or "608050"
            emit_level(out, num, rows, add, colour)
        open(OUT_DAT, "w", encoding="latin-1").write("\n".join(out) + "\n")
        print(f"wrote {OUT_DAT}: {NA} Atari levels (full)")
    else:
        # PARTIAL (validation): Atari for 1..N_ATARI, gnu for the rest, keeping all
        # gnu levels so convert_gnu_levels.py SKIP={53,58} still yields 56.
        out = ["[name]", "AtariRobbo", "[last_level]", str(max(gnu))]
        for num in sorted(gnu):
            lv = gnu[num]
            colour = lv['colour'] if lv['colour'] else "608050"
            if num <= N_ATARI:
                rows, add = atari_level_to_dat(atari[num-1], num)
            else:
                rows, add = lv['grid'], [(int(r.split('.')[0]), int(r.split('.')[1]),
                    r.split('.')[2], [int(v) for v in r.split('.')[3:]]) for r in lv['add']]
            emit_level(out, num, rows, add, colour)
        open(OUT_DAT, "w", encoding="latin-1").write("\n".join(out) + "\n")
        print(f"wrote {OUT_DAT}: {max(gnu)} levels, first {N_ATARI} from Atari")

main()
