"""Draw the title lettering once: python tools/make_logo.py [font.ttf] [size]

Renders "Solitaire" in a bold italic serif (Georgia by default) to 1 bpp and
writes tools/art/logo.txt, which is the source from then on: touch it up by
hand ('#' = ink, '.' = clear), then run tools/assets.py.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ART = Path(__file__).resolve().parent / "art"


def main():
    font = sys.argv[1] if len(sys.argv) > 1 else "C:/Windows/Fonts/georgiaz.ttf"
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    f = ImageFont.truetype(font, size)
    im = Image.new("L", (400, 100), 0)
    ImageDraw.Draw(im).text((10, 10), "Solitaire", font=f, fill=255)
    im = im.point(lambda v: 255 if v >= 112 else 0)
    im = im.crop(im.getbbox())
    rows = ["".join("#" if im.getpixel((x, y)) else "." for x in range(im.width)) for y in range(im.height)]
    head = ('# The title "Solitaire", drawn by tools/make_logo.py and touched up by hand.\n'
            "# '#' = ink, '.' = clear. Drawn with a gradient, an outline and a shadow.\n")
    (ART / "logo.txt").write_text(head + "\n".join(rows) + "\n")
    print(f"logo.txt: {im.width}x{im.height}")
    print("\n".join(rows))


if __name__ == "__main__":
    main()
