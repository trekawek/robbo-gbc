#!/usr/bin/env python3
"""Convert Altirra's raw 768-byte .pal export to the GTIA colour lookup table.

Export with Altirra 4.10's Default PAL preset (XL/XE luma map, no colour
matching): View > Adjust Colors > File > Export Palette. The export contains
16 brightness entries per hue, but a real GTIA colour register ignores bit 0.
"""
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        raise SystemExit('Usage: convert_altirra_palette.py ALTIRRA_PAL')
    data = Path(sys.argv[1]).read_bytes()
    if len(data) != 256 * 3:
        raise ValueError('Expected a 768-byte Altirra RGB .pal export')

    print('# Altirra 4.10 Default PAL (XL/XE luma, colour match None, gamma 1.00).')
    print('# Exported from View > Adjust Colors > File > Export Palette.')
    print('# GTIA ignores bit 0: odd register values use the preceding even RGB.')
    for code in range(256):
        pos = (code & 0xFE) * 3
        print(f'{code:02X} {data[pos]} {data[pos + 1]} {data[pos + 2]}')


if __name__ == '__main__':
    main()
