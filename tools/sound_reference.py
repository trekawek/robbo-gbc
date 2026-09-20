#!/usr/bin/env python3
"""Render isolated PAL Atari Robbo effects directly from the original tables.

This is an independent listening/measurement reference, not an emulation of
the Game Boy converter. It implements the POKEY configuration used by Robbo:
AUDCTL=0, independent 8-bit channels, 28 master clocks per divider tick.
The 16 table words play backwards and each lasts four PAL frames.

The polynomial registers run at the master clock, not the voice frequency.
The poly5 gate holds the previous output when closed; it does not mask the
output to zero. The gate is latched one divider event before it is used.
These details were checked against Atari800's build_poly*, advance_polies,
and event*_p917_p5 functions:
https://github.com/atari800/atari800/blob/master/src/mzpokeysnd.c

No CPU instruction delays, analogue nonlinear mixer, or other voices are
included. Global polynomial phase is arbitrary when a gameplay effect starts;
--phase selects a reproducible starting phase. AUDF changes preserve the
running divider and output latch. WAVs use one fixed gain across all effects.

Requires numpy and scipy. Example:
    python3 tools/sound_reference.py ../lkavalon-atari/robbo/d1/R1.ASM
"""

import argparse
import json
import math
from pathlib import Path
import re
import wave

import numpy as np
from scipy import signal


MASTER_HZ = 1_773_447
FRAME_CLOCKS = 114 * 312
STEP_CLOCKS = FRAME_CLOCKS * 4
NAMES = (
    "explosion", "shot", "knock", "teleport", "screw", "life", "door",
    "ammo", "push", "key", "destroy", "enter", "win", "capsule", "magnet",
)
MODES = {
    0: "poly5-gated poly17 noise",
    4: "poly17 noise",
    5: "square tone",
    6: "poly4 tone",
}


def read_tables(path):
    """Read original word order without using any converter code or output."""
    text = Path(path).read_text(encoding="latin-1")
    start = re.search(r"^\s*TABS\s+EQU\s+\*", text, re.MULTILINE)
    if not start:
        raise ValueError(f"TABS label missing from {path}")
    words = [int(match, 16) for match in re.findall(
        r"\bDTA\s+A\(\$([0-9A-Fa-f]{4})\)", text[start.end():]
    )]
    if len(words) != len(NAMES) * 16:
        raise ValueError(f"Expected 240 TABS words; found {len(words)}")
    tables = [words[index:index + 16] for index in range(0, len(words), 16)]
    for table in tables:
        for word in table:
            audc = word & 0xFF
            if audc & 15 and (audc & 0x10 or audc >> 5 not in MODES):
                raise ValueError(f"Unsupported POKEY configuration ${word:04X}")
    return tables


def polynomial(width, tap, invert=False):
    """One master-clock period, with the POKEY tap orientation/inversion."""
    mask = (1 << width) - 1
    state = 1
    bits = np.empty(mask, dtype=np.uint8)
    for index in range(mask):
        bits[index] = (state & 1) ^ int(invert)
        feedback = ((state >> tap) ^ (state >> (width - 1))) & 1
        state = ((state << 1) & mask) | feedback
    assert state == 1
    return bits


POLY4 = polynomial(4, 2, invert=True)
POLY5 = polynomial(5, 2, invert=True)
POLY17 = polynomial(17, 11)


def render_clocks(table, phase=0, tail_seconds=0.10):
    """Return linear unipolar DAC levels at one sample per master clock.

    All counters continue through silent table entries, just as they do in
    hardware. Start at the first table write rather than including the
    frame-dependent 0..4-frame delay before SOUNDV notices a new effect.
    """
    end = len(table) * STEP_CLOCKS
    output = np.zeros(end + round(tail_seconds * MASTER_HZ), dtype=np.float32)
    latch = 0
    gate = 0
    next_event = 28  # Silent AUDF=0 divider before the first table write.

    for index, word in enumerate(reversed(table)):
        start = index * STEP_CLOCKS
        stop = start + STEP_CLOCKS
        audf, audc = word >> 8, word & 0xFF
        volume = (audc & 15) / 15.0
        period = 28 * (audf + 1)
        cursor = start
        while next_event < stop:
            output[cursor:next_event] = latch * volume
            clock = next_event + phase
            if audc & 0x80 or gate:
                if audc & 0x20:
                    latch ^= 1
                elif audc & 0x40:
                    latch = int(POLY4[clock % len(POLY4)])
                else:
                    latch = int(POLY17[clock % len(POLY17)])
            gate = int(POLY5[clock % len(POLY5)])
            cursor = next_event
            next_event += period
        output[cursor:stop] = latch * volume
    return output


def render(table, rate=48_000, phase=0):
    """Bandlimit to output Nyquist, then remove DC with a 20 Hz high-pass."""
    clocks = render_clocks(table, phase=phase)
    # A short zero lead protects FFT resampling from its periodic wrap boundary.
    lead = round(0.05 * MASTER_HZ)
    padded = np.pad(clocks, (lead, 0))
    count = round(len(padded) * rate / MASTER_HZ)
    samples = signal.resample(padded, count)
    highpass = signal.butter(1, 20.0, btype="highpass", fs=rate, output="sos")
    samples = signal.sosfilt(highpass, samples)
    return samples[round(lead * rate / MASTER_HZ):]


def describe_step(word):
    audf, audc = word >> 8, word & 0xFF
    mode = audc >> 5
    result = {"word": f"{word:04X}", "audf": audf, "audc": audc,
              "volume": audc & 15, "mode": MODES.get(mode, "unused")}
    if not audc & 15:
        result["mode"] = "silence"
        return result
    divider = 28 * (audf + 1)
    result["divider_hz"] = MASTER_HZ / divider
    if mode == 5:
        result["fundamental_hz"] = MASTER_HZ / (2 * divider)
    elif mode == 6:
        bits = 15 // math.gcd(15, divider)
        result["period_bits"] = bits
        result["fundamental_hz"] = MASTER_HZ / (bits * divider)
    return result


def write_wav(path, samples, rate):
    pcm = np.rint(np.clip(samples * 0.95, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?", default=(
        Path(__file__).resolve().parents[2] / "lkavalon-atari/robbo/d1/R1.ASM"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/sound-test/reference"))
    parser.add_argument("--rate", type=int, default=48_000)
    parser.add_argument("--phase", type=int, default=0,
                        help="Starting master-clock phase of free-running polynomials")
    args = parser.parse_args()
    if args.rate < 8000:
        parser.error("--rate must be at least 8000 Hz")
    tables = read_tables(args.source)
    args.output.mkdir(parents=True, exist_ok=True)
    report = {
        "source": str(args.source), "master_hz": MASTER_HZ,
        "frame_hz": MASTER_HZ / FRAME_CLOCKS,
        "step_ms": STEP_CLOCKS * 1000 / MASTER_HZ,
        "sample_rate": args.rate, "polynomial_phase": args.phase,
        "limitations": "Isolated digital voice; fixed linear gain and 20 Hz DC "
                        "filter; no CPU write delays or analogue mixer; "
                        "gameplay starts at a variable polynomial phase.",
        "effects": [],
    }
    playlist = []
    for index, (name, table) in enumerate(zip(NAMES, tables)):
        samples = render(table, rate=args.rate, phase=args.phase)
        filename = f"{index:02d}-{name}.wav"
        write_wav(args.output / filename, samples, args.rate)
        steps = [describe_step(word) for word in reversed(table)]
        audible = [step for step in steps if step["volume"]]
        modes = sorted({step["mode"] for step in audible})
        pitches = [step["fundamental_hz"] for step in audible
                   if "fundamental_hz" in step]
        effect = {"id": index, "name": name, "wav": filename,
                  "audible_ms": len(audible) * report["step_ms"],
                  "modes": modes, "steps": steps}
        report["effects"].append(effect)
        pitch_text = (f"; tone {min(pitches):.1f}..{max(pitches):.1f} Hz"
                      if pitches else "")
        print(f"{index:2d} {name:10s} {effect['audible_ms']:7.1f} ms: "
              f"{', '.join(modes)}{pitch_text}")
        playlist.extend((samples, np.zeros(round(0.30 * args.rate))))
    write_wav(args.output / "all-effects.wav", np.concatenate(playlist), args.rate)
    (args.output / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Wrote {len(tables)} effects to {args.output}; "
          f"PAL step {report['step_ms']:.4f} ms")


if __name__ == "__main__":
    main()
