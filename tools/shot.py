#!/usr/bin/env python3
"""Boot the ROM headlessly, run frames while holding buttons, save a PNG.
Usage: shot.py rom.gbc out.png [frames] [keys]
  keys: comma list of frame:button:hold  (hold defaults to 10 frames)
        e.g. "200:right:30,260:down:20"  presses right at f200 for 30 frames."""
import sys
from pyboy import PyBoy

rom, out = sys.argv[1], sys.argv[2]
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 30
keys = sys.argv[4] if len(sys.argv) > 4 else ""

press = {}    # frame -> [buttons]
release = {}  # frame -> [buttons]
for kv in filter(None, keys.split(",")):
    parts = kv.split(":")
    f, b = int(parts[0]), parts[1]
    hold = int(parts[2]) if len(parts) > 2 else 10
    press.setdefault(f, []).append(b)
    release.setdefault(f + hold, []).append(b)

pb = PyBoy(rom, window="null", cgb=True)
for f in range(frames):
    for b in press.get(f, []):    pb.button_press(b)
    for b in release.get(f, []):  pb.button_release(b)
    pb.tick()
pb.screen.image.save(out)
pb.stop()
print("saved", out)
