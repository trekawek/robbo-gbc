"""Level conversion regressions; run with python3 tools/test_convert_atari_levels.py."""
import unittest

from convert_atari_levels import H, W, atari_level_to_dat, byte_to_cell


class CannonConversionTest(unittest.TestCase):
    def test_fixed_cannon_dispatch(self):
        # Atari d1/R2.ASM PROC dispatches these to DZ1/2/3 R,D,L,U.
        # GNU directions: E=0, S=1, W=2, N=3; shot types: bullet=0,
        # solid laser=1, blaster=2. In particular, ├ ($01) is DZ3R.
        families = (
            (0, (0x1F, 0x1D, 0x1E, 0x1C)),
            (1, (0x3E, 0x22, 0x3C, 0x5E)),
            (2, (0x01, 0x17, 0x04, 0x18)),
        )
        for shot, codes in families:
            for direction, code in enumerate(codes):
                with self.subTest(code=hex(code), direction=direction, shot=shot):
                    cell = byte_to_cell(code)
                    self.assertEqual(cell.ch, "}")
                    self.assertEqual(cell.add, [direction, direction, shot, 0, 0, 0])

    def test_level22_left_battery(self):
        # Reproduce the three cannon/box/debris rows in the original room.
        rows = [" " * W for _ in range(H)]
        rows[6] = "█├#  %%%%%%%%%$█"
        rows[7] = "█├#  %%%%%%%%█$█"
        rows[8] = "█├#  %%%%%%%%%$█"
        grid, additional = atari_level_to_dat(rows, 22)
        self.assertEqual(additional, [
            (1, y, "}", [0, 0, 2, 0, 0, 0]) for y in (6, 7, 8)
        ])
        for y in (6, 7, 8):
            self.assertEqual(grid[y][1:3], "}#")
            self.assertEqual(grid[y][5:13], "H" * 8)
            self.assertEqual(grid[y][14], "T")


class PushBoxConversionTest(unittest.TestCase):
    def test_level29_sliding_boxes(self):
        # The striped crates open the route from spawn and the exit approach.
        # Atari TBEZ pushes byte $06; BEZS returns a stopped crate to $06.
        rows = [" " * W for _ in range(H)]
        rows[2] = "█K         ╱ * █"
        rows[27] = "█ • @@@@     ╱ █"
        grid, additional = atari_level_to_dat(rows, 29)
        self.assertEqual(grid[2], "O^.........~.R.O")
        self.assertEqual(grid[27], "O.!.bbbb.....~.O")
        self.assertEqual(additional, [(1, 2, "^", [3, 0, 0])])


if __name__ == "__main__":
    unittest.main()
