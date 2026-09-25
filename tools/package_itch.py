#!/usr/bin/env python3
"""Build an itch.io HTML5 ZIP containing Robbo and binjgb."""

import argparse
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parent.parent
BINJGB_COMMIT = "c60e138da5a795ebb55e56b11b7e90024e41112c"
BINJGB_URL = f"https://raw.githubusercontent.com/binji/binjgb/{BINJGB_COMMIT}/"
ASSETS = {
    "simple.html": "docs/simple.html",
    "simple.js": "docs/simple.js",
    "simple.css": "docs/simple.css",
    "binjgb.js": "docs/binjgb.js",
    "binjgb.wasm": "docs/binjgb.wasm",
    "LICENSE.binjgb": "LICENSE",
    "LICENSE.gbstudio": "docs/LICENSE.gbstudio",
}


def get_asset(source: str, checkout: Path | None) -> bytes:
    if checkout is not None:
        return (checkout / source).read_bytes()
    request = urllib.request.Request(BINJGB_URL + source, headers={"User-Agent": "robbo-gbc-itch-packager"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def package(rom: Path, output: Path, checkout: Path | None) -> None:
    assets = {name: get_asset(source, checkout) for name, source in ASSETS.items()}

    html = assets.pop("simple.html").decode("utf-8")
    html = html.replace("<title>binjgb (simple)</title>", "<title>Robbo — Game Boy Color</title>")
    if "<script src=\"simple.js\"></script>" not in html:
        raise ValueError("binjgb simple.html changed; check its script references")
    assets["index.html"] = html.encode("utf-8")

    js = assets["simple.js"].decode("utf-8")
    js, count = re.subn(r"const ROM_FILENAME = '[^']+';", "const ROM_FILENAME = 'robbo.gbc';", js, count=1)
    if count != 1:
        raise ValueError("binjgb simple.js changed; cannot set the ROM filename")
    # The ROM already contains the Atari palette, so preserve its GBC RGB values.
    js, count = re.subn(r"const CGB_COLOR_CURVE = \d+;", "const CGB_COLOR_CURVE = 0;", js, count=1)
    if count != 1:
        raise ValueError("binjgb simple.js changed; cannot set the color curve")
    old_keys = "'KeyZ': this.setJoypB.bind(this),\n      'KeyX': this.setJoypA.bind(this),"
    new_keys = "'KeyZ': this.setJoypA.bind(this),\n      'KeyX': this.setJoypB.bind(this),"
    if js.count(old_keys) != 1:
        raise ValueError("binjgb simple.js changed; cannot set the keyboard buttons")
    js = js.replace(old_keys, new_keys)
    assets["simple.js"] = js.encode("utf-8")

    css = assets["simple.css"].decode("utf-8")
    css += "\n/* Fill the itch.io iframe while preserving the Game Boy screen ratio. */\n"
    css += "#game { align-items: center; justify-content: center; }\n"
    css += "#game canvas { width: 100%; height: 100%; }\n"
    assets["simple.css"] = css.encode("utf-8")
    assets["robbo.gbc"] = rom.read_bytes()

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, content in assets.items():
                archive.writestr(name, content)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Created {output} ({output.stat().st_size:,} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, help="use an existing ROM instead of running make")
    parser.add_argument("--output", type=Path, default=ROOT / "build/robbo-itch.zip")
    parser.add_argument("--binjgb-dir", type=Path, help="use a local binjgb checkout instead of downloading assets")
    args = parser.parse_args()

    if args.rom is None:
        subprocess.run(["make", "build/robbo.gbc"], cwd=ROOT, check=True)
    rom = args.rom or ROOT / "build/robbo.gbc"
    if not rom.is_file() or rom.stat().st_size == 0:
        parser.error(f"ROM is missing or empty: {rom}")
    try:
        package(rom, args.output, args.binjgb_dir)
    except (OSError, urllib.error.URLError, ValueError) as error:
        parser.exit(1, f"Cannot package itch.io game: {error}\n")


if __name__ == "__main__":
    main()
