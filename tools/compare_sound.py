#!/usr/bin/env python3
"""Compare Atari reference WAVs with before/after Coffee GB SFX captures.

Generate references first with sound_reference.py, then capture both versions
with SoundTest.java. This script deliberately reads rendered PCM and debugger
CSVs rather than importing the Game Boy converter. Requires numpy/scipy.

Spectral distances are Jensen-Shannon distances (0=identical, 1=disjoint),
using normalized power in logarithmic bands from 30 Hz to 20 kHz. They are a
broad timbre diagnostic, not a perceptual quality score: emulator filtering,
arbitrary noise/poly4 phase, finite clips, and timing/envelope changes all
affect the result. Listen to the generated Atari/before/after triplets too.
"""

import argparse
import csv
import json
import math
from pathlib import Path
import wave

import numpy as np
from scipy import signal
from scipy.spatial.distance import jensenshannon

from sound_reference import NAMES, write_wav


RATE = 44_100
FRAME_MS = 1000 * 70_224 / 4_194_304
BANDS = np.geomspace(30, 20_000, 49)


def read_wav(path):
    with wave.open(str(path), "rb") as source:
        if source.getsampwidth() != 2:
            raise ValueError(f"Expected 16-bit PCM in {path}")
        rate, channels = source.getframerate(), source.getnchannels()
        samples = np.frombuffer(source.readframes(source.getnframes()), dtype="<i2")
    samples = samples.astype(np.float64).reshape(-1, channels).mean(axis=1) / 32768
    if rate != RATE:
        divisor = math.gcd(rate, RATE)
        samples = signal.resample_poly(samples, RATE // divisor, rate // divisor)
    return samples


def active_span(path):
    with path.open() as source:
        active = [int(row["frame"]) for row in csv.DictReader(source)
                  if int(row["active_mask"])]
    if not active:
        raise ValueError(f"No active APU channel in {path}")
    return (active[-1] - active[0] + 1) * FRAME_MS


def align(samples, span_ms):
    # PCM batches can move the captured onset relative to the debugger CSV.
    # Detect audio onset and retain a short tail for the output DC filter.
    threshold = max(2 / 32768, np.max(np.abs(samples)) * 0.02)
    audible = np.flatnonzero(np.abs(samples) >= threshold)
    if not len(audible):
        raise ValueError("Silent recording")
    start = int(audible[0])
    end = start + round((span_ms / 1000 + 0.10) * RATE)
    return samples[start:end]


def spectral_shape(samples):
    frequencies, density = signal.welch(samples, RATE, nperseg=2048,
                                         noverlap=1024, detrend="constant")
    power = np.array([density[(frequencies >= lo) & (frequencies < hi)].sum()
                      for lo, hi in zip(BANDS[:-1], BANDS[1:])])
    power /= power.sum()
    band_centers = np.sqrt(BANDS[1:] * BANDS[:-1])
    centroid = float(np.sum(band_centers * power))
    return power, centroid


def listening_copy(samples):
    # Each version gets equal RMS for timbre listening; raw WAVs retain gain.
    active = samples[np.abs(samples) > 0.02 * np.max(np.abs(samples))]
    rms = np.sqrt(np.mean(active ** 2))
    return samples * min(0.12 / rms, 0.98 / np.max(np.abs(samples)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path,
                        default=Path("build/sound-test/reference"))
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, default=Path("build/sound-captures"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/sound-test/comparison"))
    args = parser.parse_args()
    reference = json.loads((args.reference / "analysis.json").read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    combined = []
    gap = np.zeros(round(0.30 * RATE))
    report = ["# Isolated sound comparison", "",
              "Each listening WAV plays Atari, previous GBC, then revised GBC. "
              "Playback copies have matched RMS where peak headroom permits; "
              "original captures retain their gain.", "",
              "Duration is the span from first to last active table step, including "
              "internal rests. GBC durations are measured from APU registers to one "
              "VBlank (16.743 ms). Spectral distance is a diagnostic, not a quality score.", "",
              "| Effect | Atari ms | Before ms | After ms | Spectral distance before | After |",
              "|---|---:|---:|---:|---:|---:|"]
    for index, name in enumerate(NAMES):
        basename = f"{index:02d}-{name}"
        steps = reference["effects"][index]["steps"]
        span_steps = max(i + 1 for i, step in enumerate(steps) if step["volume"])
        spans = [span_steps * reference["step_ms"],
                 active_span(args.before / f"{basename}.csv"),
                 active_span(args.after / f"{basename}.csv")]
        directories = [args.reference, args.before, args.after]
        clips = [align(read_wav(directory / f"{basename}.wav"), span)
                 for directory, span in zip(directories, spans)]
        shapes = [spectral_shape(clip) for clip in clips]
        distances = [float(jensenshannon(shapes[0][0], shape[0], base=2))
                     for shape in shapes[1:]]
        row = {"name": name, "span_ms": dict(zip(("atari", "before", "after"), spans)),
               "spectral_distance": dict(zip(("before", "after"), distances)),
               "spectral_centroid_hz": dict(zip(("atari", "before", "after"),
                                                 [shape[1] for shape in shapes]))}
        rows.append(row)
        report.append(f"| {name} | {spans[0]:.1f} | {spans[1]:.1f} | {spans[2]:.1f} "
                      f"| {distances[0]:.3f} | {distances[1]:.3f} |")
        triplet = np.concatenate([part for clip in clips
                                  for part in (listening_copy(clip), gap)])
        write_wav(args.output / f"{basename}-atari-before-after.wav", triplet, RATE)
        combined.extend((triplet, gap))
    write_wav(args.output / "all-atari-before-after.wav", np.concatenate(combined), RATE)
    report.extend(("", "Generated from an independent digital POKEY reference and "
                   "Coffee GB recordings. The reference omits Atari's analogue mixer; "
                   "the GBC recording includes emulator output filtering. Free-running "
                   "Atari polynomial phase varies in gameplay. The phase selected by "
                   "the reference is fixed for reproducibility.", ""))
    (args.output / "comparison.md").write_text("\n".join(report))
    (args.output / "comparison.json").write_text(json.dumps(rows, indent=2) + "\n")
    print("\n".join(report))


if __name__ == "__main__":
    main()
