"""Draw the tiles' letters: capitals rasterized from DejaVu Serif Bold,
anti-aliased with one in-between tone (the repository's tools/fonts/serif.py,
as CHCrossword's tools/tilefont.py).

    python tools/tilefont.py [--size 12] [--half 80] [--out FILE]

Writes tools/art/tilefont.txt, which tools/assets.py packs. Each glyph: a
line "= A", then its rows from the capitals' top: '#' ink, '+' half ink
(drawn in a tone between the letter's colour and the tile's), '.' clear.
M and W come from DejaVu Serif Condensed Bold, and W loses its first
column, so that every letter is at most 11 pixels: with its shadow it fits
a 13-pixel tile face. Q's tail runs below the baseline; J's hook is
brought up to stand on it.
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))     # the repository's tools/: fonts.serif
from fonts import serif  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=12)
    ap.add_argument("--half", type=int, default=80)
    ap.add_argument("--out", default=str(HERE / "art" / "tilefont.txt"))
    a = ap.parse_args()
    glyphs, baseline = serif.rasterize("ABCDEFGHIJKLMNOPQRSTUVWXYZ", a.size, a.half, serif.find_ttf(),
                                       narrow_ttf=serif.find_ttf("DejaVuSerifCondensed-Bold.ttf"), narrow_chars="MW",
                                       drop_first_col="W")
    serif.write_txt(a.out, ["# The tiles' letters (tools/tilefont.py: DejaVu Serif Bold, size %d; M and W" % a.size,
                            "# Condensed). '#' ink, '+' half ink. Rows from the capitals' top; the baseline is row %d." % baseline],
                    glyphs, "ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    print(f"{a.out} written")


if __name__ == "__main__":
    main()
