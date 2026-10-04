"""Draft the title lettering from system fonts into tools/art/*.txt (1 bpp,
'#' set). A starting point only: the .txt files are the source and are meant
to be touched up by hand.

    python tools/make_logo.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ART = Path(__file__).resolve().parent / "art"


def render(text, font, size, max_w, thresh=110, spacing=0):
    f = ImageFont.truetype(font, size)
    im = Image.new("L", (400, 80), 0)
    d = ImageDraw.Draw(im)
    x = 4
    for ch in text:
        d.text((x, 4), ch, 255, font=f)
        x += int(d.textlength(ch, font=f)) + spacing
    im = im.point(lambda v: 255 if v >= thresh else 0)
    im = im.crop(im.getbbox())
    assert im.width <= max_w, (text, im.width)
    return ["".join("#" if im.getpixel((x, y)) else "." for x in range(im.width)) for y in range(im.height)]


def main():
    for name, text, font, size, w, sp in (("logo", "TIC TAC TOE", "ariblk.ttf", 17, 120, 0),
                                          ("royale", "Royale", "georgiaz.ttf", 22, 90, 1)):
        rows = render(text, font, size, w, spacing=sp)
        (ART / f"{name}.txt").write_text(f"# '{text}', {len(rows[0])}x{len(rows)}\n" + "\n".join(rows) + "\n", encoding="utf-8")
        print(name, len(rows[0]), "x", len(rows))
        print("\n".join(rows))


if __name__ == "__main__":
    main()
