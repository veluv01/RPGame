"""CHSlots asset pipeline.

    python tools/assets.py           # build src/assets/*, write previews
    python tools/assets.py --export  # write tools/art/symbols.png + palette to edit

Once tools/art/symbols.png exists it is the source of the reel symbols (the
recipes below only made the first version): a transparent sheet of 22x22
cells, LUCKY 7's fifteen on the top row, DRAGON FORTUNE's ten on the second,
SWEET's eight on the third. Every
opaque pixel must be exactly a palette colour (tools/art/palette.png, .gpl);
the second and third rows use their machines' palettes. Delete the PNG to go back to
the recipes.

All of the art is new for this game:
  * tools/art/logo.txt - the "SLOTS" title lettering (1 bpp).
  * The reel symbols, 22x22 each, drawn by the recipes below (shapes on a
    small canvas, then a one-pixel ink outline) so they stay easy to adjust.
    LUCKY 7's fifteen use the casino's green palette; DRAGON FORTUNE's ten are
    drawn for its own (mach::THEMES in Machine.cpp swaps the three felt colours
    for maroon, jade and orange while that machine is on screen).

Outputs:
  src/assets/Assets.h / Assets.cpp   - generated, do not edit
  build/assets/*.png                 - previews on the real palettes
"""
import math
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ART = HERE / "art"
OUT_H = ROOT / "src" / "assets" / "Assets.h"
OUT_C = ROOT / "src" / "assets" / "Assets.cpp"
PREVIEW = ROOT / "build" / "assets"

# Must match pal::HOUSE in the RPGame library
# (platform/board/arduino/RPGame/libraries/RPGame/src/rpgame/Palette.cpp).
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
# The felts must match mach::THEMES (Machine.cpp).
FORTUNE_FELT = [0x401, 0x2A6, 0xF82]        # indices 2..4 on DRAGON FORTUNE: maroon, jade, orange
SWEET_FELT = [0xF7A, 0x7DB, 0xA6E]          # ... and on SWEET: pink, mint, lilac
THEMES = [None, FORTUNE_FELT, SWEET_FELT]   # per sheet row
# Letters used in tools/art/*.txt and the recipes. ' ' / '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16
SIZE = 22


def rgb(i, theme=0):
    """theme: 0 the casino's greens, 1 DRAGON FORTUNE, 2 SWEET (True reads as 1)."""
    felt = THEMES[int(theme)]
    c = felt[i - 2] if felt and 2 <= i <= 4 else PALETTE[i]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


# ---------------------------------------------------------------------------
# A small canvas
# ---------------------------------------------------------------------------
class Canvas:
    def __init__(self, w=SIZE, h=SIZE):
        self.w, self.h = w, h
        self.p = [[TRANSPARENT] * w for _ in range(h)]

    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = LETTER[c] if isinstance(c, str) else c

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.px(xx, yy, c)

    def ellipse(self, cx, cy, rx, ry, c, test=None):
        for y in range(self.h):
            for x in range(self.w):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0 and (test is None or test(x, y)):
                    self.px(x, y, c)

    def disc(self, cx, cy, r, c, test=None):
        self.ellipse(cx, cy, r, r, c, test)

    def ring(self, cx, cy, r0, r1, c):
        for y in range(self.h):
            for x in range(self.w):
                if r0 * r0 <= (x - cx) ** 2 + (y - cy) ** 2 <= r1 * r1:
                    self.px(x, y, c)

    def poly(self, pts, c):
        n = len(pts)
        for y in range(self.h):
            for x in range(self.w):
                inside = False
                for i in range(n):
                    (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
                    if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0) + x0:
                        inside = not inside
                if inside:
                    self.px(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.px(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n), c)

    def rows(self, x, y, rows, c=None):
        """Stamp text art: with c, '#' paints c; without, palette letters."""
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in " .":
                    continue
                self.px(x + i, y + j, c if c is not None else ch)

    def outline(self, c="k"):
        src = [r[:] for r in self.p]
        for y in range(self.h):
            for x in range(self.w):
                if src[y][x] != TRANSPARENT:
                    continue
                if any(0 <= x + dx < self.w and 0 <= y + dy < self.h and src[y + dy][x + dx] != TRANSPARENT
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    self.px(x, y, c)
        return self


# ---------------------------------------------------------------------------
# LUCKY 7
# ---------------------------------------------------------------------------
def cherry():
    c = Canvas()
    c.line(6, 12, 12, 3, "f"); c.line(7, 12, 13, 3, "f")
    c.line(15, 11, 13, 3, "f"); c.line(16, 11, 13, 4, "f")
    c.poly([(13, 1.5), (19.5, 1.5), (20.5, 4), (17, 6.5), (13, 4.5)], "g")
    c.line(14, 3, 18, 4, "f")
    c.disc(6, 15.5, 4.6, "r"); c.disc(15.5, 14.5, 4.6, "r")
    c.disc(7.5, 17, 2.6, "m", lambda x, y: (x - 6) ** 2 + (y - 15.5) ** 2 <= 4.6 ** 2 and x + y >= 24)
    c.disc(17, 16, 2.6, "m", lambda x, y: (x - 15.5) ** 2 + (y - 14.5) ** 2 <= 4.6 ** 2 and x + y >= 32)
    for x, y in ((4, 13), (5, 13), (4, 14), (13, 12), (14, 12), (13, 13)):
        c.px(x, y, "w")
    return c.outline()


def lemon():
    c = Canvas()
    c.ellipse(10.5, 12, 8.2, 6.2, "y")
    c.rect(1, 11, 2, 2, "y"); c.rect(19, 11, 2, 2, "y")
    c.ellipse(10.5, 12, 8.2, 6.2, "b", lambda x, y: ((x - 9.5) / 8.2) ** 2 + ((y - 10.6) / 6.2) ** 2 > 1.0)
    c.ellipse(7.5, 9.5, 3, 1.4, "w")
    c.poly([(10, 5.5), (12, 2), (17, 1.5), (16, 4.5), (12.5, 6.5)], "g")
    c.line(11, 5, 15, 3, "f")
    return c.outline()


def bell():
    c = Canvas()
    c.rect(10, 1, 2, 2, "y")
    c.disc(10.5, 8, 5.4, "y")
    c.poly([(5.2, 8), (16.8, 8), (18.5, 16), (3.5, 16)], "y")
    c.rect(2, 15, 18, 3, "y")
    c.rect(2, 17, 18, 1, "b")
    c.poly([(13.5, 5), (16.8, 8), (18.2, 15), (15.6, 15)], "b")
    c.line(7, 5, 6, 13, "w"); c.line(8, 4, 7, 9, "w")
    c.disc(10.5, 19.2, 1.7, "b")
    return c.outline()


BAR_B = ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."]
BAR_A = [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"]
BAR_R = ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"]


def bar():
    c = Canvas()
    c.rect(1, 5, 20, 13, "y")
    c.rect(2, 6, 18, 11, "k")
    c.rect(2, 6, 18, 1, "n"); c.rect(2, 16, 18, 1, "n")
    for i, g in enumerate((BAR_B, BAR_A, BAR_R)):
        c.rows(3 + i * 6, 8, g, "w")
    c.px(1, 5, TRANSPARENT); c.px(20, 5, TRANSPARENT); c.px(1, 17, TRANSPARENT); c.px(20, 17, TRANSPARENT)
    return c.outline()


def seven():
    c = Canvas()
    # A gold drop shadow, then the red seven with a white bevel.
    for dx, col in ((1.2, "y"), (0, "r")):
        c.poly([(2 + dx, 2 + dx), (19 + dx, 2 + dx), (19 + dx, 7 + dx), (11.5 + dx, 20 + dx), (5 + dx, 20 + dx),
                (12.5 + dx, 7.5 + dx), (6 + dx, 7.5 + dx), (6 + dx, 9.5 + dx), (2 + dx, 9.5 + dx)], col)
    c.rect(3, 3, 15, 1, "w")
    c.line(3, 3, 3, 8, "w")
    c.line(13, 8, 7, 19, "w")
    c.poly([(15.5, 7), (19, 7), (11.5, 20), (9, 20)], "m")
    c.rect(4, 8, 2, 1, "m")
    return c.outline()


def chest():
    c = Canvas()
    c.ellipse(10.5, 9, 9.4, 6.6, "b", lambda x, y: y <= 9)        # the lid
    c.rect(1, 9, 20, 10, "b")
    c.rect(1, 9, 20, 2, "y")                                       # where lid meets box
    c.rect(1, 18, 20, 1, "y")
    for x in (4, 16):
        c.rect(x, 3, 2, 16, "y")
    c.rect(1, 19, 20, 1, "k")
    c.rect(9, 9, 4, 5, "y"); c.rect(10, 11, 2, 2, "k")             # the lock
    c.line(3, 6, 7, 3, "p"); c.line(13, 3, 15, 3, "p")             # light on the lid
    c.rect(2, 13, 2, 1, "p"); c.rect(18, 13, 2, 1, "k"); c.rect(18, 15, 2, 2, "k")
    c.px(7, 6, "x"); c.px(6, 6, "w"); c.px(8, 6, "w"); c.px(7, 5, "w"); c.px(7, 7, "w")   # a glint
    return c.outline()


def plum():
    c = Canvas()
    c.line(11, 5, 13, 1, "b"); c.line(12, 5, 14, 1, "b")
    c.poly([(13, 3.5), (18, 1.5), (19.5, 4), (15, 6)], "g")
    c.ellipse(10.5, 13, 7.6, 7.2, "m")
    c.ellipse(8, 10.5, 3.2, 3.6, "r")
    c.line(11, 6, 12, 12, "k")
    c.px(6, 9, "w"); c.px(7, 8, "w"); c.px(6, 10, "w")
    return c.outline()


def grape():
    c = Canvas()
    c.rect(10, 0, 2, 4, "b")
    c.poly([(12, 1.5), (18, 0.5), (19, 3.5), (14, 5)], "g")
    for x, y in ((5, 7), (10.5, 6.5), (16, 7), (7.7, 11.5), (13.3, 11.5), (10.5, 16.5)):
        c.disc(x, y, 3.1, "u")
        c.disc(x + 0.9, y + 0.9, 1.7, "n", lambda px, py, x=x, y=y: (px - x) ** 2 + (py - y) ** 2 <= 3.1 ** 2)
        c.px(round(x - 1.4), round(y - 1.4), "c")
    return c.outline()


def watermelon():
    c = Canvas()
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - 10.5, y - 5.5)
            if y < 6 or d > 10.4:
                continue
            c.px(x, y, "d" if d > 9.4 else "f" if d > 8.2 else "w" if d > 7.4 else "r")
    for x, y in ((6, 8), (10, 7), (15, 8), (8, 11), (13, 11), (11, 14)):
        c.px(x, y, "k"); c.px(x, y + 1, "k")
    return c.outline()


def banana():
    c = Canvas()
    for y in range(SIZE):
        for x in range(SIZE):
            a = math.hypot(x - 7, y - 5)
            b = math.hypot(x - 3, y - 0.5)
            if a <= 13.2 and b >= 14.2 and x <= 20 and y <= 20:
                c.px(x, y, "b" if a > 11.8 else "y")
    for x, y in ((19, 2), (20, 2), (19, 3), (20, 3), (2, 18), (3, 19), (2, 19)):
        if c.p[y][x] != TRANSPARENT:
            c.px(x, y, "k")
    c.line(15, 9, 9, 15, "w")
    return c.outline()


def clover():
    c = Canvas()
    c.line(11, 12, 15, 20, "d"); c.line(12, 12, 16, 20, "f")
    for x, y in ((6.5, 6.5), (14.5, 6.5), (6.5, 14.5), (14.5, 14.5)):
        c.disc(x, y, 4.3, "f")
        c.disc(x - 1, y - 1, 1.8, "g")
    c.rect(9, 9, 4, 4, "f")
    c.line(4, 4, 9, 9, "d"); c.line(17, 4, 12, 9, "d"); c.line(4, 17, 9, 12, "d"); c.line(17, 17, 12, 12, "d")
    return c.outline()


def horseshoe():
    c = Canvas()
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - 10.5, y - 9.5)
            arc = y <= 9.5 and 5.2 <= d <= 9.4
            leg = 9.5 < y <= 19 and (1.1 <= x <= 5.3 or 15.7 <= x <= 19.9)
            if arc or leg:
                c.px(x, y, "s")
    c.rect(1, 18, 6, 2, "s"); c.rect(15, 18, 6, 2, "s")
    for x, y in ((3, 13), (3, 8), (6, 4), (10, 2), (11, 2), (15, 4), (18, 8), (18, 13)):
        c.px(x, y, "k")
    for y in range(SIZE):                       # light from the left, shade on the right leg
        for x in range(SIZE):
            if c.p[y][x] == LETTER["s"]:
                if x <= 2 and y > 6: c.px(x, y, "w")
                elif x >= 19 and y > 6: c.px(x, y, "n")
    c.rect(1, 19, 6, 1, "n"); c.rect(15, 19, 6, 1, "n")
    return c.outline()


def diamond():
    c = Canvas()
    c.poly([(5, 3.5), (16.5, 3.5), (20.5, 9), (10.75, 20.5), (0.5, 9)], "c")
    c.poly([(14, 9), (20.5, 9), (10.75, 20.5)], "u")
    c.poly([(5, 3.5), (10.5, 3.5), (7, 9), (0.5, 9)], "w")
    for a, b in (((1, 9), (20, 9)), ((7, 9), (10, 19)), ((14, 9), (11, 19)), ((10, 4), (7, 9)), ((11, 4), (14, 9)),
                 ((16, 4), (14, 9))):
        c.line(*a, *b, "u")
    c.px(4, 6, "x")
    return c.outline()


def apple():
    c = Canvas()
    c.rect(10, 1, 2, 5, "b")
    c.poly([(12, 4.5), (17, 1), (18.5, 3.5), (14, 6)], "g")
    c.disc(7, 11, 5.6, "r"); c.disc(14, 11, 5.6, "r")
    c.ellipse(10.5, 14, 7, 6, "r")
    for y in range(SIZE):
        for x in range(SIZE):
            if c.p[y][x] == LETTER["r"] and math.hypot(x - 8.5, y - 11) > 8.2:
                c.px(x, y, "m")
    c.px(10, 7, "m"); c.px(11, 7, "m")
    c.px(5, 9, "w"); c.px(6, 8, "w"); c.px(5, 10, "w"); c.px(4, 11, "w")
    return c.outline()


def orange():
    c = Canvas()
    c.poly([(11, 4.5), (15, 0.5), (18, 2.5), (14, 5.5)], "g")
    c.disc(10.5, 12.5, 8.2, "p")
    c.disc(10.5, 12.5, 8.2, "b", lambda x, y: math.hypot(x - 9.2, y - 11.2) > 7.6)
    c.px(10, 5, "b"); c.px(11, 5, "b"); c.px(10, 6, "b")
    c.ellipse(7, 9.5, 2.2, 1.4, "w")
    for x, y in ((12, 10), (8, 14), (13, 15), (10, 12)):
        c.px(x, y, "y")
    return c.outline()


# ---------------------------------------------------------------------------
# DRAGON FORTUNE (f = jade, g = orange, d = maroon here)
# ---------------------------------------------------------------------------
def jade():
    c = Canvas()
    c.ring(10.5, 10.5, 3.4, 9.2, "f")
    c.ring(10.5, 10.5, 8.2, 9.2, "n")
    for a in range(200, 260, 4):
        r = math.radians(a)
        c.px(round(10.5 + 6.6 * math.cos(r)), round(10.5 + 6.6 * math.sin(r)), "w")
    for a in range(20, 70, 5):
        r = math.radians(a)
        c.px(round(10.5 + 6.2 * math.cos(r)), round(10.5 + 6.2 * math.sin(r)), "c")
    return c.outline()


def fan():
    c = Canvas()
    cx, cy = 10.5, 18.5
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - cx, y - cy)
            a = math.degrees(math.atan2(cy - y, x - cx))
            if 22 <= a <= 158:
                if 6 <= d <= 16.4:
                    c.px(x, y, "r" if int((a - 22) / 17) % 2 == 0 else "m")
                elif d < 6:
                    c.px(x, y, "b")
                if 15.2 <= d <= 16.4:
                    c.px(x, y, "y")
    for a in (22, 56, 90, 124, 158):
        r = math.radians(a)
        c.line(round(cx), round(cy), round(cx + 15.5 * math.cos(r)), round(cy - 15.5 * math.sin(r)), "y")
    c.disc(10.5, 18.5, 1.6, "y")
    return c.outline()


def lantern():
    c = Canvas()
    c.rect(10, 0, 2, 2, "y")
    c.rect(7, 2, 8, 2, "y")
    c.ellipse(10.5, 9.5, 8.2, 6.2, "r")
    for x in (5, 10, 11, 16):
        for y in range(4, 16):
            if c.p[y][x] == LETTER["r"]:
                c.px(x, y, "g" if x in (10, 11) else "m")
    c.ellipse(6.5, 7.5, 1.6, 2.4, "w")
    c.rect(7, 15, 8, 2, "y")
    c.rect(10, 17, 2, 2, "y")
    for x in (8, 10, 11, 13):
        c.line(x, 18, x, 21, "g")
    return c.outline()


def envelope():
    c = Canvas()
    c.rect(4, 1, 14, 20, "r")
    c.rect(4, 1, 14, 1, "g"); c.rect(4, 20, 14, 1, "m")
    c.rect(17, 2, 1, 19, "m")
    c.poly([(4, 2), (18, 2), (11, 8.5)], "m")
    c.line(4, 2, 10, 8, "y"); c.line(17, 2, 11, 8, "y")
    c.disc(10.5, 12.5, 4.2, "y")
    c.rect(9, 11, 4, 4, "r")
    c.rect(10, 12, 2, 2, "y")
    return c.outline()


def koi():
    c = Canvas()
    c.poly([(1, 3), (6, 7), (7, 12), (2, 13), (4, 8)], "g")              # tail
    c.ellipse(12, 12, 8.5, 5.2, "w")
    c.ellipse(9, 10, 4, 3, "g"); c.ellipse(15.5, 13.5, 3, 2.4, "g")
    c.ellipse(17.5, 9.5, 2, 1.4, "r")
    c.poly([(9, 6.5), (14, 3), (15, 7.5)], "g")                          # dorsal fin
    c.poly([(11, 16), (13, 20), (15, 16.5)], "g")
    c.px(18, 12, "k"); c.px(18, 11, "k")
    c.px(20, 13, "r")
    return c.outline()


def ingot():
    c = Canvas()
    c.poly([(1, 7), (5, 10), (17, 10), (21, 7), (19.5, 17), (15, 19.5), (7, 19.5), (2.5, 17)], "y")
    c.ellipse(10.5, 9.5, 5.6, 4.6, "y")
    c.ellipse(10.5, 9.5, 5.6, 4.6, "b", lambda x, y: ((x - 10.5) / 4.6) ** 2 + ((y - 8.6) / 3.8) ** 2 > 1 and y >= 10)
    c.poly([(15, 19.5), (19.5, 17), (20.6, 9), (17.5, 15)], "b")
    c.rect(7, 18, 9, 1, "b")
    c.ellipse(8.5, 7.5, 2.2, 1.3, "w")
    c.line(3, 10, 4, 15, "w")
    return c.outline()


def cat():
    c = Canvas()
    c.poly([(4, 1), (8.5, 4.5), (4, 8)], "w"); c.poly([(18, 1), (13.5, 4.5), (18, 8)], "w")
    c.px(5, 4, "r"); c.px(5, 5, "r"); c.px(16, 4, "r"); c.px(16, 5, "r")
    c.ellipse(11, 8.5, 7.2, 5.8, "w")
    c.ellipse(11, 16.5, 6.4, 4.8, "w")
    c.rect(1, 2, 4, 9, "w"); c.rect(1, 2, 4, 1, "s")                     # the beckoning paw
    c.rect(5, 12, 13, 2, "r")                                            # collar
    c.disc(11, 14.2, 1.6, "y")
    c.rect(7, 7, 2, 1, "k"); c.px(7, 6, "k"); c.px(8, 8, "k")            # happy eyes
    c.rect(13, 7, 2, 1, "k"); c.px(14, 6, "k"); c.px(13, 8, "k")
    c.px(11, 9, "r"); c.px(10, 10, "k"); c.px(12, 10, "k")
    c.rect(8, 16, 6, 4, "y"); c.rect(9, 17, 4, 2, "b")                   # the koban coin it holds
    return c.outline()


DRAGON = [
    ".y..y..........y..y.",
    ".yy.yy........yy.yy.",
    "..yyyy..ffff..yyyy..",
    "...yyffffffffffyy...",
    "..rrffffffffffffrr..",
    ".rrfwwkffffffkwwfrr.",
    ".rrfwwkffffffkwwfrr.",
    "rrrffffffffffffffrrr",
    ".rrfffkkffffkkfffrr.",
    "..rffffffffffffffr..",
    "..gfwfwfwfwfwfwffg..",
    ".gg.ffffffffffff.gg.",
    ".g....rrrrrrrr....g.",
]
WILD = [
    "#...#.###.#...##.",
    "#...#..#..#...#.#",
    "#.#.#..#..#...#.#",
    "##.##..#..#...#.#",
    "#...#.###.###.##.",
]


def dragon():
    c = Canvas()
    c.rows(1, 1, DRAGON)
    c.outline()
    c.rows(3, 16, WILD, "y")
    for x in range(2, 21):                          # ink behind the word so it reads on any reel
        for y in range(15, 22):
            if c.p[y][x] == TRANSPARENT:
                c.px(x, y, "k")
    return c


def gong():
    c = Canvas()
    c.rect(1, 1, 20, 2, "r")
    c.rect(1, 1, 2, 20, "r"); c.rect(19, 1, 2, 20, "r")
    c.rect(0, 20, 5, 2, "m"); c.rect(17, 20, 5, 2, "m")
    c.line(7, 3, 7, 5, "s"); c.line(14, 3, 14, 5, "s")
    c.disc(10.5, 11.5, 7.2, "y")
    c.ring(10.5, 11.5, 4.6, 5.4, "b")
    c.disc(10.5, 11.5, 7.2, "b", lambda x, y: (x - 9.6) ** 2 + (y - 10.6) ** 2 > 6.6 ** 2)
    c.disc(10.5, 11.5, 1.8, "w")
    c.px(7, 7, "w"); c.px(8, 6, "w"); c.px(6, 8, "w")
    return c.outline()


COIN_DOLLAR = ["..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."]     # the value is printed under it


def coin():
    c = Canvas()
    c.disc(10.5, 10.5, 10.4, "y")
    c.ring(10.5, 10.5, 9.2, 10.4, "b")
    c.ring(10.5, 10.5, 7.0, 7.6, "b")
    for a in range(195, 255, 3):
        r = math.radians(a)
        c.px(round(10.5 + 8.5 * math.cos(r)), round(10.5 + 8.5 * math.sin(r)), "w")
    c.rows(8, 2, COIN_DOLLAR, "b")
    return c


# ---------------------------------------------------------------------------
# SWEET (d = pink, f = mint, g = lilac here)
# ---------------------------------------------------------------------------
def gumdrop():
    c = Canvas()
    c.ellipse(10.5, 13, 8, 9.5, "f", lambda x, y: y <= 18)
    c.rect(2, 17, 18, 3, "f")
    c.ellipse(10.5, 13, 8, 9.5, "u", lambda x, y: y <= 18 and math.hypot(x - 8.5, y - 11) > 8.6)
    c.rect(3, 19, 16, 1, "u")
    for x, y in ((6, 8), (10, 6), (14, 9), (8, 12), (12, 13), (5, 15), (15, 15), (10, 17)):
        c.px(x, y, "w")
    return c.outline()


def candy():
    c = Canvas()
    c.poly([(0.5, 5), (6, 11), (0.5, 17)], "d"); c.poly([(21.5, 5), (16, 11), (21.5, 17)], "d")
    c.line(1, 8, 4, 11, "w"); c.line(20, 8, 17, 11, "w")
    c.ellipse(10.5, 11, 6.4, 5.6, "r")
    for k in (-8, -3, 2):
        for t in range(-6, 7):
            x, y = round(10.5 + k + t * 0.6 + 3), 11 + t
            if c.p[y][x] == LETTER["r"]:
                c.px(x, y, "w")
            if c.p[y][x + 1] == LETTER["r"]:
                c.px(x + 1, y, "w")
    c.px(7, 8, "w")
    return c.outline()


def chocolate():
    c = Canvas()
    c.rect(4, 1, 14, 12, "b")
    for x in (8, 13):
        c.rect(x, 1, 1, 12, "k")
    for y in (4, 8):
        c.rect(4, y, 14, 1, "k")
    for x in (5, 10, 15):
        for y in (2, 6, 10):
            c.px(x, y, "p")
    c.rect(3, 12, 16, 9, "r")                   # the wrapper, torn back
    c.rect(3, 12, 16, 2, "s"); c.px(5, 12, "w"); c.px(11, 13, "w"); c.px(16, 12, "w")
    c.rect(3, 16, 16, 2, "y")
    c.rect(17, 14, 2, 7, "m")
    return c.outline()


def donut():
    c = Canvas()
    c.ring(10.5, 11, 2.6, 9.4, "p")
    c.ring(10.5, 11, 2.6, 9.4, "b")
    c.ring(10.5, 10.2, 2.6, 8.6, "p")
    for y in range(SIZE):                       # pink icing over the top, dripping
        for x in range(SIZE):
            d = math.hypot(x - 10.5, y - 10.2)
            if 3.4 <= d <= 7.8 and y <= 12 + (x % 3):
                c.px(x, y, "d")
    for x, y, col in ((6, 6, "w"), (10, 4, "c"), (15, 6, "y"), (4, 10, "y"), (17, 10, "w"), (7, 12, "c"),
                      (14, 12, "u"), (12, 5, "u")):
        c.px(x, y, col); c.px(x + 1, y, col)
    return c.outline()


def cupcake():
    c = Canvas()
    c.poly([(3, 11.5), (19, 11.5), (16.5, 20.5), (5.5, 20.5)], "u")
    for x in (6, 9, 12, 15):
        c.line(x, 12, x + (1 if x < 10 else -1) * 0, 20, "c")
    c.ellipse(10.5, 9.5, 8.6, 3.6, "g")
    c.ellipse(10.5, 6.5, 6.2, 3, "g")
    c.ellipse(10.5, 4, 3.6, 2.2, "g")
    c.line(4, 10, 16, 8, "w"); c.line(6, 7, 14, 5, "w")
    c.disc(10.5, 1.6, 1.7, "r")
    return c.outline()


def icecream():
    c = Canvas()
    c.poly([(5, 11), (17, 11), (11, 21.5)], "p")
    c.line(7, 12, 12, 19, "b"); c.line(10, 12, 13, 16, "b"); c.line(15, 12, 10, 19, "b"); c.line(12, 12, 9, 16, "b")
    c.disc(10.5, 7, 5.6, "d")
    c.rect(5, 9, 12, 3, "d")
    for x in (5, 8, 11, 14):
        c.px(x, 12, "d"); c.px(x + 1, 12, "d")
    c.disc(8.5, 5, 1.6, "w")
    c.disc(10.5, 7, 5.6, "m", lambda x, y: math.hypot(x - 9.2, y - 6) > 5.6 and y < 10)
    c.disc(11, 1.2, 1.4, "r")
    return c.outline()


def bear():
    c = Canvas()
    c.disc(6, 3.5, 2.2, "r"); c.disc(15, 3.5, 2.2, "r")
    c.disc(10.5, 6.5, 5.2, "r")
    c.ellipse(10.5, 14.5, 5.6, 5.6, "r")
    c.ellipse(4, 12, 2.4, 2, "r"); c.ellipse(17, 12, 2.4, 2, "r")
    c.ellipse(6.5, 19.5, 2.4, 1.8, "r"); c.ellipse(14.5, 19.5, 2.4, 1.8, "r")
    for y in range(SIZE):                       # jelly: a darker side, a shine
        for x in range(SIZE):
            if c.p[y][x] == LETTER["r"] and x >= 13 and (x + y) > 24:
                c.px(x, y, "m")
    c.px(8, 6, "k"); c.px(13, 6, "k"); c.px(10, 8, "k"); c.px(11, 8, "k")
    c.px(8, 4, "w"); c.px(7, 5, "w"); c.px(7, 12, "w"); c.px(7, 13, "w"); c.px(8, 12, "w")
    c.ellipse(10.5, 15, 2.6, 3, "d")
    return c.outline()


def lolly():
    c = Canvas()
    c.rect(10, 11, 2, 5, "w")
    for y in range(SIZE):
        for x in range(SIZE):
            dx, dy = x - 10.5, y - 7
            d = math.hypot(dx, dy)
            if d <= 6.8:
                a = math.atan2(dy, dx)
                c.px(x, y, "r" if int((a + d * 0.55) / (math.pi / 3) + 12) % 2 == 0 else "w")
    c.ring(10.5, 7, 6.0, 6.8, "d")
    c.outline()
    c.rows(3, 16, WILD, "d")
    for x in range(2, 21):                      # ink behind the word so it reads on any reel
        for y in range(15, 22):
            if c.p[y][x] == TRANSPARENT:
                c.px(x, y, "k")
    return c


SWEET = [("gumdrop", gumdrop), ("candy", candy), ("chocolate", chocolate), ("donut", donut), ("cupcake", cupcake),
         ("icecream", icecream), ("bear", bear), ("lolly", lolly)]
CLASSIC = [("cherry", cherry), ("lemon", lemon), ("bell", bell), ("bar", bar), ("seven", seven), ("chest", chest),
           ("plum", plum), ("grape", grape), ("watermelon", watermelon), ("banana", banana), ("clover", clover),
           ("horseshoe", horseshoe), ("diamond", diamond), ("apple", apple), ("orange", orange)]
FORTUNE = [("jade", jade), ("fan", fan), ("lantern", lantern), ("envelope", envelope), ("koi", koi),
           ("ingot", ingot), ("cat", cat), ("dragon", dragon), ("gong", gong), ("coin", coin)]


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------
def pack_rows1(bits):
    """1 bpp rows -> MSB-first row bytes."""
    out = []
    for row in bits:
        for x0 in range(0, len(row), 8):
            b = 0
            for i in range(8):
                if x0 + i < len(row) and row[x0 + i]:
                    b |= 0x80 >> i
            out.append(b)
    return out


def pack_span4(img, trans=TRANSPARENT):
    """Colour image -> w, h, then per row: n, then n bytes of (len-1)<<4 | colour:
    RPGfx's sprite4 format (gfx_sprite4). Transparent runs use colour 15, which
    gfx_sprite4 skips, so the art never draws FX_B."""
    h, w = len(img), len(img[0])
    out = [w, h]
    for row in img:
        assert 15 not in row, "FX_B is the transparent colour"
        runs, x = [], 0
        while x < w:
            c = row[x]
            s = x
            while x < w and row[x] == c and x - s < 16:
                x += 1
            runs.append((x - s, c))
        while runs and runs[-1][1] == trans:        # trailing transparency is implicit
            runs.pop()
        out.append(len(runs))
        for n, c in runs:
            out.append(((n - 1) << 4) | (15 if c == trans else c))
    return out


GROUPS = (CLASSIC, FORTUNE, SWEET)             # the sheet's rows


def sheet(name, imgs, bg, fortune, scale=6):
    """All the symbols of one machine on its reel colour."""
    n = len(imgs)
    im = Image.new("RGB", (n * 24, 24), rgb(bg, fortune))
    for i, img in enumerate(imgs):
        for y in range(SIZE):
            for x in range(SIZE):
                if img[y][x] != TRANSPARENT:
                    im.putpixel((i * 24 + 1 + x, 1 + y), rgb(img[y][x], fortune))
    PREVIEW.mkdir(parents=True, exist_ok=True)
    im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


class Out:
    def __init__(self):
        self.h, self.c = [], []

    def array(self, name, data, ctype="uint8_t", comment=""):
        if comment:
            self.c.append(f"// {comment}")
        body = ",".join(f"0x{v:02X}" if ctype == "uint8_t" else str(v) for v in data)
        lines = [body[i:i + 110] for i in range(0, len(body), 110)]
        self.c.append(f"const {ctype} {name}[{len(data)}] = {{\n  " + "\n  ".join(lines) + "\n};")
        self.h.append(f"extern const {ctype} {name}[{len(data)}];")

    def const(self, name, value):
        self.h.append(f"constexpr int {name} = {value};")

    def write(self):
        hdr = ("// GENERATED by tools/assets.py - do not edit.\n"
               "// Art drawn for CHSlots, 2026, bateske (tools/art/, tools/assets.py).\n")
        OUT_H.parent.mkdir(parents=True, exist_ok=True)
        OUT_H.write_text(hdr + "#pragma once\n#include <stdint.h>\n\n" + "\n".join(self.h) + "\n")
        OUT_C.write_text(hdr + '#include "Assets.h"\n\n' + "\n".join(self.c) + "\n")


def load_bits(name):
    """tools/art/<name>.txt: '#' = set, anything else clear; ';' starts a comment line."""
    rows = [ln for ln in (ART / f"{name}.txt").read_text().splitlines() if ln and not ln.startswith(";")]
    w = max(len(r) for r in rows)
    return [[1 if x < len(r) and r[x] == "#" else 0 for x in range(w)] for r in rows]


SHEET = ART / "symbols.png"
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]


def load_sheet():
    """tools/art/symbols.png -> {(row, col): image}, checked against the palette."""
    im = Image.open(SHEET).convert("RGBA")
    out = {}
    for row, group in enumerate(GROUPS):
        lut = {rgb(i, row): i for i in range(14, -1, -1)}           # FX_B is GOLD's colour: use GOLD
        for col, (name, fn) in enumerate(group):
            if im.height < (row + 1) * SIZE or im.width < (col + 1) * SIZE:
                out[(row, col)] = fn().p                            # not in this sheet yet: the recipe
                continue
            img = []
            for y in range(SIZE):
                line = []
                for x in range(SIZE):
                    r, g, b, a = im.getpixel((col * SIZE + x, row * SIZE + y))
                    if a == 0:
                        line.append(TRANSPARENT)
                    elif a == 255 and (r, g, b) in lut:
                        line.append(lut[(r, g, b)])
                    else:
                        raise SystemExit(f"symbols.png {name} ({x},{y}): #{r:02X}{g:02X}{b:02X} alpha {a} "
                                         "is not a palette colour")
                img.append(line)
            out[(row, col)] = img
    return out


def export():
    """The sheet as a transparent PNG, and the palette as a swatch and a .gpl."""
    png = load_sheet() if SHEET.exists() else None
    im = Image.new("RGBA", (max(len(g) for g in GROUPS) * SIZE, len(GROUPS) * SIZE), (0, 0, 0, 0))
    for row, group in enumerate(GROUPS):
        for col, (name, fn) in enumerate(group):
            img = png[(row, col)] if png else fn().p
            for y in range(SIZE):
                for x in range(SIZE):
                    if img[y][x] != TRANSPARENT:
                        im.putpixel((col * SIZE + x, row * SIZE + y), rgb(img[y][x], row) + (255,))
    im.save(SHEET)
    # Swatch: the 16 colours, then DRAGON FORTUNE's three replacements under theirs.
    from PIL import ImageDraw
    cell = 40
    sw = Image.new("RGB", (16 * cell, 3 * cell + 24), (40, 40, 40))
    d = ImageDraw.Draw(sw)
    for i in range(16):
        d.rectangle([i * cell, 0, i * cell + cell - 1, cell - 1], fill=rgb(i))
        d.text((i * cell + 2, 3 * cell + 2), NAMES[i][:6], fill=(255, 255, 255))
        if 2 <= i <= 4:
            d.rectangle([i * cell, cell, i * cell + cell - 1, 2 * cell - 1], fill=rgb(i, 1))
            d.rectangle([i * cell, 2 * cell, i * cell + cell - 1, 3 * cell - 1], fill=rgb(i, 2))
    sw.save(ART / "palette.png")
    gpl = ["GIMP Palette", "Name: CHSlots", "Columns: 16", "#"]
    for i in range(15):
        gpl.append("%3d %3d %3d	%s" % (rgb(i) + (NAMES[i],)))
    for th, names in ((1, ("FORTUNE maroon (FELT_DK)", "FORTUNE jade (FELT)", "FORTUNE orange (FELT_LT)")),
                      (2, ("SWEET pink (FELT_DK)", "SWEET mint (FELT)", "SWEET lilac (FELT_LT)"))):
        for i, nm in zip((2, 3, 4), names):
            gpl.append("%3d %3d %3d	%s" % (rgb(i, th) + (nm,)))
    (ART / "palette.gpl").write_text("\n".join(gpl) + "\n")
    print(f"wrote {SHEET.relative_to(ROOT)}, tools/art/palette.png, tools/art/palette.gpl")


def main():
    import sys
    if "--export" in sys.argv:
        export()
        return
    png = load_sheet() if SHEET.exists() else None
    o = Out()
    data, at = [], []
    for row, (group, bg, fortune) in enumerate(((CLASSIC, 1, 0), (FORTUNE, 2, 1), (SWEET, 1, 2))):
        imgs = []
        for col, (name, fn) in enumerate(group):
            img = png[(row, col)] if png else fn().p
            assert len(img) == SIZE and all(len(r) == SIZE for r in img), name
            at.append(len(data))
            data += pack_span4(img)
            imgs.append(img)
        sheet(("classic", "fortune", "sweet")[row], imgs, bg, fortune)
    o.array("SYMBOLS", data, comment="reel symbols 22x22, row spans: " +
            " ".join(n for n, _ in CLASSIC + FORTUNE + SWEET))
    o.array("SYMBOL_AT", at, "uint16_t", comment="where each symbol starts in SYMBOLS")
    o.const("SYM_SIZE", SIZE)
    o.const("SYM_FORTUNE", len(CLASSIC))
    o.const("SYM_SWEET", len(CLASSIC) + len(FORTUNE))

    logo = load_bits("logo")
    o.array("LOGO", pack_rows1(logo), comment=f"'SLOTS' title lettering {len(logo[0])}x{len(logo)}, MSB-first rows")
    o.const("LOGO_W", len(logo[0]))
    o.const("LOGO_H", len(logo))

    o.write()
    print(f"wrote {OUT_H.relative_to(ROOT)} and {OUT_C.relative_to(ROOT)}: symbols {len(data)} B")


if __name__ == "__main__":
    main()
