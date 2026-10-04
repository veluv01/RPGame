"""The look-dev contact sheet: the board at each zoom and in each colour
treatment, labelled, in one picture.

    python tools/lookdev.py            -> out/look/contact.png
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = ROOT / "out" / "look"

ROWS = [
    ("RESTING ZOOM (the art is drawn for one of these; landings zoom in to double it)",
     [("zoom5", "A1  cells 20x10"), ("zoom6", "A2  cells 24x12"),
      ("zoom8", "A3  cells 32x16"), ("zoom10", "(landing close-up of A1)")]),
    ("TILES AND THE PINK / ORANGE BANDS, AT REST",
     [("white_dither", "B1  white+silver, dithered"), ("cream_dither", "B2  cream+white, dithered"),
      ("white_solid", "B3  white+silver, solid"), ("cream_solid", "B4  cream+white, solid")]),
    ("... AND CLOSE UP",
     [("white_dither_close", "B1"), ("cream_dither_close", "B2"),
      ("white_solid_close", "B3"), ("cream_solid_close", "B4")]),
    ("ROUND THE BOARD (B1 at A1)",
     [("jail", "Jail corner"), ("parking", "Free Parking"), ("gotojail", "Go To Jail"), ("right", "the near right side")]),
]


def main():
    r = subprocess.run([sys.executable, str(HERE / "chsim" / "chdrive.py"), "--sim", str(ROOT),
                        str(HERE / "scripts" / "look.txt"), str(OUT)])
    if r.returncode:
        raise SystemExit(r.returncode)
    cell, pad, head, cap = 384, 10, 22, 18
    w = pad + 4 * (cell + pad)
    h = pad + len(ROWS) * (head + cell + cap + pad)
    sheet = Image.new("RGB", (w, h), (24, 24, 32))
    d = ImageDraw.Draw(sheet)
    y = pad
    for title, shots in ROWS:
        d.text((pad, y + 4), title, fill=(255, 204, 34))
        y += head
        for i, (name, label) in enumerate(shots):
            x = pad + i * (cell + pad)
            sheet.paste(Image.open(OUT / f"{name}.png").convert("RGB"), (x, y))
            d.text((x + 2, y + cell + 3), label, fill=(255, 255, 255))
        y += cell + cap + pad
    sheet.save(OUT / "contact.png")
    print(OUT / "contact.png")


if __name__ == "__main__":
    main()
