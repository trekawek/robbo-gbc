#!/usr/bin/env python3
"""Capture native Atari800 PAL colours and verify all 56 Robbo room palettes.

Needs atari800, mads, Xvfb, and Python packages python-xlib, pexpect, Pillow.
Example:
  python3 tools/atari_palette_reference.py \
    /home/newton/dev/lkavalon-atari/robbo /tmp/robbo-colors-reference

Uses an isolated, freshly generated emulator config. PNGs come directly from
Atari800's F10 screenshot writer; their indexed palette is never converted by
X11, ffmpeg, a video codec, or a display colour profile. The supplied original
executable must match the supplied assembler sources. Monitor writes only select
rooms and enter their original initialization routine; the game code is unchanged.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import time

import pexpect
from PIL import Image
from Xlib import X, XK, display
from Xlib.ext import xtest


def metadata(root):
    result = []
    for filename in ('C1', 'C2', 'C3'):
        for line in (root / 'd2' / (filename + '.txt')).read_text().splitlines():
            match = re.match(r'metadata:\s*([0-9a-fA-F]+)', line)
            if match:
                result.append(bytes.fromhex(match.group(1)))
    assert len(result) == 56
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('atari_source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--display', default=':93')
    args = parser.parse_args()
    root, out = args.atari_source.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    expected = metadata(root)
    subprocess.run(['mads', 'd1/R1.ASM', '-o:' + str(out / 'reference.obx'),
                    '-t:' + str(out / 'reference.lab')], cwd=root, check=True,
                   stdout=subprocess.DEVNULL)
    labels = {}
    for line in (out / 'reference.lab').read_text().splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] == '00':
            labels[fields[2]] = int(fields[1], 16)
    # CHNGCV: LDA TMR / BNE / LDA #7 / STA TMR / INC CNTR = 13 bytes.
    breakpoint = labels['CHNGCV'] + 13
    config = out / 'reference.cfg'
    config.unlink(missing_ok=True)
    capture = out / 'capture.png'
    capture.unlink(missing_ok=True)
    version = subprocess.run(['atari800', '-v'], capture_output=True, text=True).stdout.strip()
    options = ['-config', str(config), '-no-autosave-config', '-windowed',
               '-nosound', '-pal', '-colors-preset', 'standard',
               '-pal-artif', 'none', '-scanlines', '0', '-screenshots',
               str(capture), '-run', str(root / 'bin' / 'robbo.xex')]
    xvfb = subprocess.Popen(['Xvfb', args.display, '-screen', '0', '800x600x24'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    emu = None
    rows = []
    full_palette = None
    try:
        time.sleep(.4)
        if xvfb.poll() is not None:
            raise RuntimeError('Xvfb could not start; choose another --display')
        emu = pexpect.spawn('atari800', options,
                            env=dict(os.environ, DISPLAY=args.display),
                            encoding='utf-8', timeout=15)
        emu.delaybeforesend = .01
        time.sleep(2)
        xdisplay = display.Display(args.display)

        def key(name, down=True):
            code = xdisplay.keysym_to_keycode(XK.string_to_keysym(name))
            xtest.fake_input(xdisplay, X.KeyPress if down else X.KeyRelease, code)
            xdisplay.sync()

        def tap(name):
            key(name)
            time.sleep(.05)
            key(name, False)

        def command(text):
            emu.sendline(text)
            emu.expect(r'(?m)^> ')
            return emu.before

        def memory(addr):
            result = command('m %x' % addr)
            match = re.search(r'%04X: ((?:[0-9A-F]{2} ){15}[0-9A-F]{2})' % addr, result)
            if not match:
                raise RuntimeError(result)
            return bytes.fromhex(match.group(1))

        tap('F8')
        emu.expect(r'(?m)^> ')
        assert memory(labels['CHNGCV'])[5:7] == bytes([0xA9, 7]), 'Executable/source mismatch'
        command('bpc %x' % breakpoint)
        emu.sendline('cont')
        time.sleep(.1)
        tap('F4')
        emu.expect(r'(?m)^> ')
        for level, expected_bytes in enumerate(expected, 1):
            if level != 1:
                command('c %x %x' % (labels['CNUM'], level - 1))
                command('setpc %x' % labels['SETU'])
                command('sets fd')
                command('cont')
            live_metadata = memory(labels['CAVS'] + (level - 1) * 512 + 496)
            live = memory(707)[:6]  # COLB, COLPF0..3, COLBK shadow registers.
            actual = list(live[1:]) + [live[0]]
            want = list(expected_bytes[2:8])
            assert live_metadata == expected_bytes, (level, 'room data differs', live_metadata, expected_bytes)
            assert actual == want, (level, 'palette differs', actual, want)
            # Native keyboard events need a running SDL window. Temporarily
            # release the gameplay breakpoint while F10 writes the indexed PNG.
            command('bpc 0')
            emu.sendline('cont')
            time.sleep(.2)
            tap('F10')
            time.sleep(.05)
            tap('F8')
            emu.expect(r'(?m)^> ')
            command('bpc %x' % breakpoint)
            if not capture.exists():
                raise RuntimeError('Native screenshot was not written for level %d' % level)
            destination = out / ('level-%02d.png' % level)
            capture.replace(destination)
            image = Image.open(destination)
            assert image.mode == 'P', 'Need native indexed PNG output'
            palette = image.getpalette()
            if full_palette is None:
                full_palette = palette
            assert palette == full_palette, 'Native palette changed between rooms'
            # Record the actual colour indices used by the rendered status bar.
            # Atari800 standard 336x240 crop has the status bar at y=194..209.
            hud_indices = sorted(set(image.crop((8, 194, 328, 210)).tobytes()))
            expected_hud = sorted({actual[4] & 0xFE, (actual[4] & 0xF0) | 0x0A})
            assert hud_indices == expected_hud, (level, 'HUD colours differ', hud_indices, expected_hud)
            rows.append(dict(level=level, COLPF0=actual[0], COLPF1=actual[1],
                             COLPF2=actual[2], COLPF3=actual[3], COLBK=actual[4],
                             COLB=actual[5], hud_indices=hud_indices))
            if level % 8 == 0:
                print('Verified/captured %d / 56 levels' % level, flush=True)
        emu.sendline('quit')
        emu.expect(pexpect.EOF)
    finally:
        if emu is not None:
            emu.close(force=True)
        xvfb.terminate()
        xvfb.wait(timeout=5)

    with (out / 'palettes.csv').open('w') as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    with (out / 'palette-native.txt').open('w') as handle:
        handle.write('# Native Atari800 standard PAL PNG palette. GTIA registers ignore bit 0.\n')
        for index in range(256):
            rgb = full_palette[index * 3:index * 3 + 3]
            handle.write('%02X %d %d %d\n' % (index, *rgb))
    with (out / 'palette-gtia.txt').open('w') as handle:
        handle.write('# ' + version + ' standard PAL native PNG palette, colorbyte R G B.\n')
        handle.write('# Fresh config, -pal -colors-preset standard -pal-artif none.\n')
        handle.write('# GTIA ignores bit 0: odd entries alias their even neighbours.\n')
        for index in range(256):
            rgb = full_palette[(index & 0xFE) * 3:(index & 0xFE) * 3 + 3]
            handle.write('%02X %d %d %d\n' % (index, *rgb))
    (out / 'reference.json').write_text(json.dumps(dict(
        emulator=version, options=options, breakpoint=breakpoint,
        register_order=['COLPF0', 'COLPF1', 'COLPF2', 'COLPF3', 'COLBK', 'COLB'],
        levels=rows), indent=2) + '\n')
    print('All 56 metadata palettes match live Atari registers; native PNGs and RGB table: %s' % out)


if __name__ == '__main__':
    main()
