#!/usr/bin/env python3
"""Convert Robbo's 15 POKEY effects (R1.ASM TABS) to native GBC APU steps.

SOUNDV reads each table backwards, once per four PAL VBlanks. AUDCTL=0:
POKEY divider events occur at 1773447 / (28 * (AUDF + 1)) Hz. $A is a
square, $C samples the free-running 4-bit polynomial, $8 samples poly17,
and $0 samples poly17 only when the poly5 gate permits (otherwise holds).

Step bytes: {kind | original volume, period lo / NR43, period hi, wave id}.
Kind 0=square, 0x40=wave, 0x80=noise. Volume zero always means silence.
Wave RAM images include volume, preserving the envelope without NR32's
coarse volume shifts. The noise approximation searches every NR43 divisor;
GBC cannot reproduce POKEY's 17-bit polynomial or its poly5 gate exactly.
"""
import math
import os
import re
import sys

NSOUNDS = 15
NSTEPS = 16
PAL_CLOCK = 1773447
DIVIDER = 28
SQUARE = 0x00
WAVE = 0x40
NOISE = 0x80
NAMES = ["explosion", "shot", "knock", "teleport", "screw", "life", "door",
         "ammo", "push", "key", "destroy", "enter", "win", "capsule", "magnet"]
# POKEY's polynomial output sequence, clocked at the MASTER clock, not AUDF.
# See Atari800 src/pokeysnd.c bit4 and src/mzpokeysnd.c advance_polies.
POLY4 = (1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0)


def parse_tabs(path):
    with open(path, encoding="latin-1") as source:
        lines = source.read().splitlines()
    i = next(k for k, line in enumerate(lines) if re.match(r'\s*TABS\s+EQU', line))
    words = []
    for line in lines[i + 1:]:
        match = re.search(r'DTA\s+A\(\$([0-9A-Fa-f]{4})\)', line)
        if match:
            words.append(int(match.group(1), 16))
            if len(words) == NSOUNDS * NSTEPS:
                break
    if len(words) != NSOUNDS * NSTEPS:
        raise ValueError(f"expected {NSOUNDS * NSTEPS} words, got {len(words)}")
    return [words[s * NSTEPS:(s + 1) * NSTEPS] for s in range(NSOUNDS)]


def gb_period(hz, wave=False):
    clock = 65536 if wave else 131072
    # Compare the two neighbouring periods in pitch space.
    ideal = 2048 - clock / hz
    candidates = {max(0, min(2047, int(x))) for x in (math.floor(ideal), math.ceil(ideal))}
    return min(candidates, key=lambda x: abs(math.log2((clock / (2048 - x)) / hz)))


def gb_square_freq(audf):
    return gb_period(PAL_CLOCK / (2 * DIVIDER * (audf + 1)))


def noise_rate(poly):
    divisor = (8, 16, 32, 48, 64, 80, 96, 112)[poly & 7]
    return 4194304 / (divisor << (poly >> 4))


def gb_noise_poly(audf, gated=False):
    rate = PAL_CLOCK / (DIVIDER * (audf + 1))
    if gated:
        # 15 of poly5's 31 states permit a new output bit. Approximate the
        # resulting held noise's bandwidth; do not mistake it for pure $8.
        rate *= 15 / 31
    return min((shift << 4 | ratio for shift in range(14) for ratio in range(8)),
               key=lambda p: abs(math.log2(noise_rate(p) / rate)))


def poly4_pattern(audf):
    stride = DIVIDER * (audf + 1)
    length = 15 // math.gcd(stride, 15)
    return tuple(POLY4[(i * stride) % 15] for i in range(length))


def wave_bytes(bits, volume):
    """Area-average one polynomial period into 32 four-bit wave samples.

    Fractional edges preserve the duty and reduce aliasing compared with
    rounding each of the 15 (or 5/3) POKEY events to whole GBC samples.
    """
    n = len(bits)
    samples = []
    for i in range(32):
        start, end = i * n / 32, (i + 1) * n / 32
        area = sum(bits[j % n] * max(0, min(end, j + 1) - max(start, j))
                   for j in range(math.floor(start), math.ceil(end)))
        samples.append(max(0, min(15, round(volume * area * 32 / n))))
    return tuple((samples[i] << 4) | samples[i + 1] for i in range(0, 32, 2))


def step_bytes(word, waves):
    audf, audc = word >> 8, word & 255
    volume, distortion = audc & 15, audc & 0xF0
    if not volume:
        return (0, 0, 0, 0)
    if distortion == 0xA0:
        period = gb_square_freq(audf)
        return (SQUARE | volume, period & 255, period >> 8, 0)
    if distortion == 0xC0:
        bits = poly4_pattern(audf)
        if len(set(bits)) == 1:  # Divider repeatedly samples the same poly4 bit: DC.
            return (0, 0, 0, 0)
        hz = PAL_CLOCK / (DIVIDER * (audf + 1) * len(bits))
        period = gb_period(hz, wave=True)
        wave = wave_bytes(bits, volume)
        if wave not in waves:
            waves.append(wave)
        return (WAVE | volume, period & 255, period >> 8, waves.index(wave))
    if distortion in (0x00, 0x80):
        return (NOISE | volume, gb_noise_poly(audf, distortion == 0), 0, 0)
    raise ValueError(f"unsupported Robbo AUDC ${audc:02X}")


def convert(sounds):
    waves = []
    sequences = [[step_bytes(word, waves) for word in reversed(sound)] for sound in sounds]
    return sequences, waves


def main():
    src, outdir = sys.argv[1:]
    sequences, waves = convert(parse_tabs(os.path.join(src, "d1", "R1.ASM")))
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "sounds.h"), "w") as header:
        header.write("#ifndef SOUNDS_H\n#define SOUNDS_H\n")
        header.write("/* Generated by tools/convert_sound.py from R1.ASM TABS. */\n")
        header.write(f"#define SND_NSTEPS {NSTEPS}\n#define SND_COUNT {NSOUNDS}\n")
        header.write("#define SND_STEP_BYTES 4\n#define SND_WAVE 0x40\n#define SND_NOISE 0x80\n")
        header.write("static const unsigned char SND_SEQ[SND_COUNT][SND_NSTEPS * SND_STEP_BYTES] = {\n")
        for name, sequence in zip(NAMES, sequences):
            header.write("    {" + ",".join(f"0x{b:02X}" for step in sequence for b in step)
                         + f"}}, /* {name} */\n")
        header.write("};\nstatic const unsigned char SND_WAVES[][16] = {\n")
        for wave in waves:
            header.write("    {" + ",".join(f"0x{b:02X}" for b in wave) + "},\n")
        header.write("};\n#endif\n")
    print(f"sounds: {NSOUNDS} x {NSTEPS} steps, {len(waves)} waveforms -> {outdir}/sounds.h")


if __name__ == "__main__":
    main()
