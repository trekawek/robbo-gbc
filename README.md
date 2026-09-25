# ROBBO — Game Boy Color

<img src="docs/robbo-gbc-cover-first-1024.png" width="25%"/>

Robbo is a classic puzzle game created in 1989 for Atari 8-bit computers. It was
a major success in Poland and is still [considered one of the country's best games](https://technologia.dziennik.pl/gry/artykuly/595048,robbo-wiedzmin-3-polskie-gry-komputerowe.html).

Robbo has appeared on many platforms, including through the open-source
[GNU Robbo](https://gnurobbo.sourceforge.net/) project. This Game Boy Color port
stays faithful to the Atari original, with 56 levels featuring Sokoban-style box
pushing, monsters, cannons, and magnets.

Enjoy!

![ROBBO gameplay on Game Boy Color](docs/gameplay.gif)

## Play in a browser on itch.io

Run `make itch` to build `build/robbo-itch.zip`. The target builds the ROM, then
uses `tools/package_itch.py` to download a pinned version of
[binjgb](https://github.com/binji/binjgb) and bundles its web frontend, WASM
runtime, licenses, and `robbo.gbc`. You can also run the script directly. To
package an existing ROM without rebuilding, pass `--rom path/to/robbo.gbc`. For
offline packaging, pass `--binjgb-dir` with the path to a local binjgb checkout.

Upload the ZIP to itch.io as an **HTML Game** and choose **Embed in page** with a
640 × 576 viewport, or **Click to launch in fullscreen**. The ZIP has `index.html`
at its root and uses relative asset paths. Use arrow keys to move, **Z** for A,
**X** for B, **Enter** for Start, and **Tab** for Select. Touch controls appear on
touch devices.
