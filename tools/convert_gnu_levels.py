#!/usr/bin/env python3
"""Convert a gnu-robbo .dat pack into ROM-resident C for the GBC port.

Level grid/additional data (~34KB) is too big for one 16KB ROM bank, so it is
split across several banked modules (levels_dN.c, #pragma bank 255).  A HOME
index (levels_idx.c) holds the small scalar tables + pointer arrays + a
gr_level_bankswitch(idx) that SWITCH_ROMs to the bank holding that level.  The
loader calls gr_level_bankswitch(idx) once, then reads grid+add (same bank).

Additional record (10 bytes): x,y,symbol(ascii),valcount,v0..v5
Usage: convert_gnu_levels.py original.dat OUTDIR
"""
import sys

dat, outdir = sys.argv[1], sys.argv[2]
text = [l.rstrip('\r\n') for l in open(dat, encoding='latin-1')]

last_level = 0
levels = {}
default_colour = 0x608050
i = 0
cur = None
mode = None
while i < len(text):
    line = text[i]
    if line == '[name]': i += 2; continue
    if line == '[last_level]': last_level = int(text[i+1]); i += 2; continue
    if line == '[default_level_colour]': default_colour = int(text[i+1], 16); i += 2; mode=None; continue
    if line == '[level]':
        cur = {'w':0,'h':0,'colour':None,'grid':[],'add':[]}; levels[int(text[i+1])] = cur; i += 2; mode=None; continue
    if line == '[colour]': cur['colour'] = int(text[i+1], 16); i += 2; mode=None; continue
    if line == '[size]':
        w,h = text[i+1].split('.'); cur['w']=int(w); cur['h']=int(h); i+=2; mode=None; continue
    if line == '[author]': i += 2; mode=None; continue
    if line == '[level_notes]': i += 1; mode='skip'; continue
    if line == '[data]': mode='data'; i += 1; continue
    if line == '[additional]':
        mode=None; i += 1; cnt = int(text[i]); i += 1
        for _ in range(cnt):
            p = text[i].split('.'); i += 1
            cur['add'].append((int(p[0]), int(p[1]), p[2], [int(v) for v in p[3:]]))
        continue
    if line == '[end]': mode=None; i += 1; continue
    if mode == 'data':
        if line == '' or line.startswith('['): mode=None; continue
        cur['grid'].append(line); i += 1; continue
    if mode == 'skip':
        if line.startswith('['): mode=None; continue
        i += 1; continue
    i += 1

# The Atari original Robbo has 56 levels; gnu-robbo's original.dat has 58 - level
# 53 is an inserted level and 58 is appended (identified by matching wall layouts
# against the Atari level data).  Drop them so the set matches the Atari 56 and
# the per-level Atari palettes (gr_atari_pal) align 1:1.  Then renumber 1..56.
# The Atari pack (convert_atari_levels.py) already has exactly 56 levels, so skip
# nothing there; only the raw gnu-robbo file (58 levels) needs its 2 extras dropped.
SKIP = {53, 58} if last_level == 58 else set()
kept = [n for n in sorted(levels) if n not in SKIP]
levels = {i + 1: levels[n] for i, n in enumerate(kept)}
last_level = len(kept)

N = last_level
MOD_BYTES = 13000   # cap per banked module

# build per-level flat data
flat_grid = {}
flat_add = {}
ws = [0]*N; hs = [0]*N; cols = [0]*N; addns = [0]*N
for n in range(1, N+1):
    lv = levels.get(n)
    if not lv: continue
    w, h = lv['w'], lv['h']
    ws[n-1]=w; hs[n-1]=h
    cols[n-1] = lv['colour'] if lv['colour'] is not None else default_colour
    g = lv['grid']
    flat = []
    for r in range(h):
        row = g[r] if r < len(g) else ''
        for cx in range(w):
            flat.append(ord(row[cx]) if cx < len(row) else ord('.'))
    flat_grid[n] = flat
    fa = []
    for (x,y,sym,vals) in lv['add']:
        vv = (vals + [0]*6)[:6]
        fa += [x, y, ord(sym), len(vals)] + vv
    flat_add[n] = fa
    addns[n-1] = len(lv['add'])

# assign levels to banked modules
modules = []   # list of list-of-level-numbers
cur_mod = []; cur_bytes = 0
for n in range(1, N+1):
    sz = len(flat_grid.get(n, [])) + len(flat_add.get(n, []))
    if cur_bytes + sz > MOD_BYTES and cur_mod:
        modules.append(cur_mod); cur_mod=[]; cur_bytes=0
    cur_mod.append(n); cur_bytes += sz
if cur_mod: modules.append(cur_mod)
NMOD = len(modules)
mod_of = {}
for m, lv_list in enumerate(modules):
    for n in lv_list: mod_of[n] = m

def carr(name, data):
    return 'const unsigned char %s[%d] = {%s};' % (name, len(data), ','.join(str(b) for b in data))

# emit banked modules
for m, lv_list in enumerate(modules):
    out = ['#pragma bank 255', '#include <gb/gb.h>', '#include "levels_data.h"',
           'BANKREF(levels_d%d)' % m, '']
    for n in lv_list:
        if n in flat_grid: out.append(carr('gr_grid_%d' % n, flat_grid[n]))
        if flat_add.get(n): out.append(carr('gr_add_%d' % n, flat_add[n]))
    open('%s/levels_d%d.c' % (outdir, m), 'w').write('\n'.join(out) + '\n')

# header
h = ['#ifndef GR_LEVELS_DATA_H', '#define GR_LEVELS_DATA_H', '#include <gb/gb.h>',
     '#define GR_NLEVELS %d' % N]
for m in range(NMOD): h.append('BANKREF_EXTERN(levels_d%d)' % m)
h += ['extern const unsigned char gr_level_w[GR_NLEVELS];',
      'extern const unsigned char gr_level_h[GR_NLEVELS];',
      'extern const unsigned long gr_level_colour[GR_NLEVELS];',
      'extern const unsigned char gr_level_addn[GR_NLEVELS];',
      'extern const unsigned char * const gr_level_grid[GR_NLEVELS];',
      'extern const unsigned char * const gr_level_add[GR_NLEVELS];',
      'void gr_level_bankswitch(int idx);', '#endif']
open(outdir + '/levels_data.h', 'w').write('\n'.join(h) + '\n')

# HOME index
c = ['#include "levels_data.h"']
# extern the banked grid/add arrays
for n in range(1, N+1):
    if n in flat_grid: c.append('extern const unsigned char gr_grid_%d[];' % n)
    if flat_add.get(n): c.append('extern const unsigned char gr_add_%d[];' % n)
c.append('const unsigned char gr_level_w[GR_NLEVELS] = {%s};' % ','.join(str(v) for v in ws))
c.append('const unsigned char gr_level_h[GR_NLEVELS] = {%s};' % ','.join(str(v) for v in hs))
c.append('const unsigned long gr_level_colour[GR_NLEVELS] = {%s};' % ','.join('0x%06XUL'%v for v in cols))
c.append('const unsigned char gr_level_addn[GR_NLEVELS] = {%s};' % ','.join(str(v) for v in addns))
grid_ptrs = ['gr_grid_%d' % n if n in flat_grid else '0' for n in range(1, N+1)]
add_ptrs  = ['gr_add_%d' % n if flat_add.get(n) else '0' for n in range(1, N+1)]
c.append('const unsigned char * const gr_level_grid[GR_NLEVELS] = {%s};' % ','.join(grid_ptrs))
c.append('const unsigned char * const gr_level_add[GR_NLEVELS] = {%s};' % ','.join(add_ptrs))
# bankswitch dispatcher (idx is 0-based)
c.append('void gr_level_bankswitch(int idx) {')
for m, lv_list in enumerate(modules):
    lo, hi = lv_list[0]-1, lv_list[-1]-1
    c.append('    if (idx >= %d && idx <= %d) { SWITCH_ROM(BANK(levels_d%d)); return; }' % (lo, hi, m))
c.append('}')
open(outdir + '/levels_idx.c', 'w').write('\n'.join(c) + '\n')

print('levels: %d in %d banked modules -> %s/levels_{idx,dN}.c' % (N, NMOD, outdir))
