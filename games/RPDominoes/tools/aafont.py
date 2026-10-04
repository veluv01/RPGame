"""The game's serif lettering: CHCrossword's anti-aliased capitals (DejaVu
Serif Bold at 12 px, capitals 9 pixels tall; the repository's
tools/fonts/serif.py), with the figures and stops this game also prints
drawn the same way. At size 12 the capitals come out identical to
CHCrossword's tools/art/tilefont.txt.

    python tools/aafont.py [--ttf PATH] [--size 12] [--half 80] [--out FILE]
    python tools/aafont.py --preview                 # out/aafont.png: sizes compared

Writes tools/art/aafont.txt, which tools/assets.py packs and which can be
touched up by hand afterwards (this only needs running again to start over
from the typeface). DejaVu is freely redistributable (see NOTICE). Each
glyph: a line "= c", then its rows from the capitals' top: '#' ink, '+'
half ink, '.' clear. J's hook is brought up onto the baseline, and nothing
goes more than two rows below it (Q's tail).
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))     # the repository's tools/: fonts.serif
from fonts import serif  # noqa: E402

CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!'+-.:?"


def glyphs(path, size, half):
    return serif.rasterize(CHARS, size, half, path, max_below_baseline=2, base=24, canvas=48)


def preview(path, half):
    pal = {"INK": (0, 0, 0), "WHITE": (255, 255, 255), "SILVER": (187, 187, 204), "NAVY": (17, 34, 85),
           "GOLD": (255, 204, 34), "WOOD": (119, 68, 17), "FELT": (17, 119, 51), "FELT_DK": (0, 68, 34)}
    lines = [("OPTIONS", "GOLD", "WOOD", "FELT"), ("SOUND ON", "WHITE", "SILVER", "FELT"),
             ("1 PLAYER", "GOLD", "WOOD", "NAVY"), ("PLAY TO 100", "WHITE", "SILVER", "NAVY"),
             ("FIFTEEN!", "GOLD", "WOOD", "FELT"), ("YOUR ROUND!", "GOLD", "WOOD", "NAVY")]
    sizes = [12, 13, 14]
    W, H = 3 * 130, len(lines) * 20
    im = Image.new("RGB", (W, H))
    for k, size in enumerate(sizes):
        g, _ = glyphs(path, size, half)
        for j, (text, c, mid, bg) in enumerate(lines):
            ox, oy = k * 130, j * 20
            for y in range(20):
                for x in range(128):
                    im.putpixel((ox + x, oy + y), pal[bg])
            x = ox + 3
            for ch in text:
                if ch == " ":
                    x += 4
                    continue
                for r, row in enumerate(g[ch]):
                    for i, v in enumerate(row):
                        if v != ".":
                            im.putpixel((x + i, oy + 4 + r), pal[c if v == "#" else mid])
                x += len(g[ch][0]) + 1
    im.resize((W * 3, H * 3), Image.NEAREST).save(ROOT / "out" / "aafont.png")
    print("out/aafont.png: sizes", sizes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf")
    ap.add_argument("--size", type=int, default=12)
    ap.add_argument("--half", type=int, default=80)
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--out", default=str(HERE / "art" / "aafont.txt"))
    a = ap.parse_args()
    path = serif.find_ttf(explicit=a.ttf)
    if a.preview:
        (ROOT / "out").mkdir(exist_ok=True)
        preview(path, a.half)
        return
    g, baseline = glyphs(path, a.size, a.half)
    serif.write_txt(a.out, [f"# The serif lettering (tools/aafont.py: DejaVu Serif Bold, size {a.size}).",
                            f"# '#' ink, '+' half ink. Rows from the capitals' top; the baseline is row {baseline}."],
                    g, CHARS)
    print(f"{a.out}: {len(CHARS)} glyphs, size {a.size}")


if __name__ == "__main__":
    main()
