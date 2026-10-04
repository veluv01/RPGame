"""The look-dev contact sheet: the board at each size and in each colour
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
    ("THE WHOLE BOARD: THE SQUARES' TONES",
     [("zoom5", "felt, numbered (as shipped)"), ("zoom5_plain", "felt, no numbers"),
      ("navy", "navy + blue"), ("light", "light felt")]),
    ("CLOSE UP",
     [("close27", "round 27"), ("close99", "the top left"), ("close4", "the bottom left"), ("close54", "the middle")]),
    ("BETWEEN THE TWO SIZES (the whip zoom passes through these)",
     [("zoom7", "zoom 7: art at 1x"), ("zoom8", "zoom 8: art at 2x"), ("white", "white + silver"), ("zoom5", "")]),
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
