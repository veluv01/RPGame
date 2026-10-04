"""Look-dev: the board mocked up in Python, to choose colours and chip art
before (and beside) the real renderer in Table.cpp.

    python tools/lookdev.py            -> docs/mockups/board_*.png

Not part of the build. The layout numbers mirror Table.h.
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "docs" / "mockups"

PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
(INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE,
 GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B) = range(16)
THEMES = {"green": (0x042, 0x173, 0x4B5), "blue": (0x024, 0x149, 0x48D),
          "red": (0x401, 0x812, 0xC44), "purple": (0x203, 0x517, 0x95B)}


def rgb(c):
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


class Fb:
    def __init__(self):
        self.p = [[FELT] * 128 for _ in range(128)]

    def px(self, x, y, c):
        if 0 <= x < 128 and 0 <= y < 128:
            self.p[y][x] = c

    def rect(self, x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.px(i, j, c)

    def image(self, theme, scale=3):
        pal = list(PALETTE)
        pal[FELT_DK], pal[FELT], pal[FELT_LT] = THEMES[theme]
        im = Image.new("RGB", (128, 128))
        for y in range(128):
            for x in range(128):
                im.putpixel((x, y), rgb(pal[self.p[y][x]]))
        return im.resize((128 * scale, 128 * scale), Image.NEAREST)


# Layout (world = screen at 1:1).
TOP, BOT = 10, 118                 # board rows [TOP, BOT)
FIELD_T, FIELD_B = 13, 115         # the playing field's rows
LEFT_X, RIGHT_X = 2, 64            # the two halves' first columns
PW, PH = 9, 42                     # a point's width and height
PITCH = 7


def point_x(w):
    """White point 1..24 -> its left column."""
    if w <= 6:
        return RIGHT_X + (6 - w) * PW
    if w <= 12:
        return LEFT_X + (12 - w) * PW
    if w <= 18:
        return LEFT_X + (w - 13) * PW
    return RIGHT_X + (w - 19) * PW


CHIPS = {
    "dome": ["..kkkk..",
             ".kaaaak.",
             "kahaaaak",
             "kaaaaaak",
             "kaaaaadk",
             "kaaaaddk",
             ".kadddk.",
             "..kkkk.."],
    "ring": ["..kkkk..",
             ".kaaaak.",
             "kaaddaak",
             "kadaadak",
             "kadaadak",
             "kaaddaak",
             ".kaaaak.",
             "..kkkk.."],
    "spots": ["..kkkk..",
              ".keaaek.",
              "keaaaaek",
              "kaaddaak",
              "kaaddaak",
              "keaaaaek",
              ".keaaek.",
              "..kkkk.."],
    "soft": ["..rrrr..",
             ".rhaaar.",
             "rhaaaaar",
             "raaaaaar",
             "raaaaadr",
             "raaaaddr",
             ".raaddr.",
             "..rrrr.."],
}
SIDES = {
    "white-red": ({"k": INK, "a": WHITE, "h": WHITE, "d": SILVER, "e": BLUE, "r": SILVER},
                  {"k": INK, "a": RED, "h": SKIN, "d": WINE, "e": WHITE, "r": WINE}),
    "white-navy": ({"k": INK, "a": WHITE, "h": WHITE, "d": SILVER, "e": RED, "r": SILVER},
                   {"k": INK, "a": BLUE, "h": CYAN, "d": NAVY, "e": WHITE, "r": NAVY}),
    "gold-red": ({"k": INK, "a": GOLD, "h": WHITE, "d": WOOD, "e": WHITE, "r": WOOD},
                 {"k": INK, "a": RED, "h": SKIN, "d": WINE, "e": WHITE, "r": WINE}),
}


def chip(fb, x, y, art, tones):
    for j, row in enumerate(art):
        for i, ch in enumerate(row):
            if ch != ".":
                fb.px(x + i, y + j, tones[ch])


def die(fb, x, y, face, body, shade, pip):
    fb.rect(x + 1, y, 10, 12, body)
    fb.rect(x, y + 1, 12, 10, body)
    fb.rect(x + 1, y + 11, 10, 1, shade)
    fb.rect(x + 11, y + 1, 1, 10, shade)
    spots = {1: [4], 2: [0, 8], 3: [0, 4, 8], 4: [0, 2, 6, 8], 5: [0, 2, 4, 6, 8], 6: [0, 2, 3, 5, 6, 8]}[face]
    for s in spots:
        fb.rect(x + 2 + 3 * (s % 3), y + 2 + 3 * (s // 3), 2, 2, pip)


def board(points, chipname, sides, start=None):
    fb = Fb()
    art = CHIPS[chipname]
    tones = SIDES[sides]
    # HUD and footer.
    fb.rect(0, 0, 128, 9, INK)
    fb.rect(0, 9, 128, 1, GOLD)
    fb.rect(0, 118, 128, 10, INK)
    fb.rect(0, 118, 128, 1, GOLD)
    # Wood, field, inlay.
    fb.rect(0, TOP, 128, BOT - TOP, WOOD)
    for x0 in (LEFT_X, RIGHT_X):
        fb.rect(x0, FIELD_T, 6 * PW, FIELD_B - FIELD_T, FELT)
    fb.rect(119, FIELD_T, 8, PH + 2, INK)
    fb.rect(119, FIELD_B - PH - 2, 8, PH + 2, INK)
    fb.rect(1, TOP + 2, 117, 1, GOLD)
    fb.rect(1, BOT - 3, 117, 1, GOLD)
    # Points.
    for w in range(1, 25):
        x0 = point_x(w)
        c = points[(w + (w > 12)) & 1]
        for d in range(PH):
            inset = (PW * d) // (2 * PH)
            y = FIELD_T + d if w > 12 else FIELD_B - 1 - d
            fb.rect(x0 + inset, y, PW - 2 * inset, 1, c)
    # Checkers: the starting position, a checker each on the bar, some off.
    pos = start or {0: {24: 2, 13: 5, 8: 3, 6: 5}, 1: {24: 2, 13: 5, 8: 3, 6: 5}}
    for s in (0, 1):
        for p, n in pos[s].items():
            w = p if s == 0 else 25 - p
            x = point_x(w)
            pitch = PITCH if n <= 5 else (4 * PITCH * 16) // (n - 1) / 16
            for k in range(n):
                y = FIELD_T + int(k * pitch) if w > 12 else FIELD_B - 8 - int(k * pitch)
                chip(fb, x, y, art, tones[s])
    chip(fb, 56, 40, art, tones[0])
    chip(fb, 56, 80, art, tones[1])
    for k in range(4):
        fb.rect(119, FIELD_B - 3 - 3 * k, 8, 2, tones[0]["a"])
        fb.rect(119, FIELD_B - 1 - 3 * k, 8, 1, tones[0]["d"])
        fb.rect(119, FIELD_T + 1 + 3 * k, 8, 2, tones[1]["a"])
        fb.rect(119, FIELD_T + 3 + 3 * k, 8, 1, tones[1]["d"])
    die(fb, 78, 58, 6, WHITE, SILVER, INK)
    die(fb, 93, 58, 4, WHITE, SILVER, INK)
    die(fb, 16, 58, 3, RED, WINE, WHITE)
    die(fb, 31, 58, 1, RED, WINE, WHITE)
    return fb


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    variants = [
        ((FELT_DK, FELT_LT), "dome", "white-red"),
        ((WINE, WOOD), "dome", "white-navy"),
        ((NAVY, GOLD), "dome", "white-red"),
        ((FELT_DK, GOLD), "ring", "white-red"),
        ((WINE, SKIN), "spots", "white-navy"),
        ((FELT_DK, FELT_LT), "soft", "white-navy"),
        ((NAVY, WINE), "dome", "gold-red"),
        ((FELT_DK, SKIN), "dome", "white-red"),
    ]
    themes = sys.argv[1:] or ["green", "red"]
    for theme in themes:
        sheet = Image.new("RGB", (4 * 390, 2 * 390), (20, 20, 20))
        for i, (pts, art, sides) in enumerate(variants):
            im = board(pts, art, sides).image(theme)
            sheet.paste(im, ((i % 4) * 390 + 3, (i // 4) * 390 + 3))
        sheet.save(OUT / f"board_{theme}.png")
        print(OUT / f"board_{theme}.png")


if __name__ == "__main__":
    main()
