"""Look-dev for the tiles: a few pip treatments side by side, at the game's
close-up size (13 x 25) on the felt, enlarged. Writes out/tilemock.png.

    python tools/tilemock.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PAL = {"INK": 0x000, "WHITE": 0xFFF, "FELT_DK": 0x042, "FELT": 0x173, "SILVER": 0xBBC, "BONE": 0xEEE,
       "SLATE": 0x445, "NAVY": 0x125, "RED": 0xE12, "BLUE": 0x26E, "WOOD": 0x741, "WINE": 0x702}
PIPMASK = [0x000, 0x010, 0x101, 0x111, 0x145, 0x155, 0x1C7]
CLASSIC = ["INK", "BLUE", "RED", "WOOD", "NAVY", "WINE", "INK"]


def rgb(name):
    c = PAL[name]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


def tile(a, b, style, dark=False, depth=3):
    """Rows of colour names (None = clear), upright 13 x 25 plus depth."""
    W, H = 13, 25
    face, hi, lo, pip = ("SLATE", "SILVER", "NAVY", "WHITE") if dark else ("BONE", "WHITE", "SILVER", "SLATE")
    img = [[None] * W for _ in range(H + depth)]
    for r in range(H + depth):
        for c in range(W):
            edge = c in (0, W - 1) or r in (0, H - 1, H + depth - 1)
            corner = c in (0, W - 1) and r in (0, H + depth - 1)
            if corner:
                continue
            if edge:
                img[r][c] = "INK"
            elif r >= H:
                img[r][c] = lo
            elif r == 1 or c == 1:
                img[r][c] = hi
            elif r == H - 2 or c == W - 2:
                img[r][c] = lo
            elif r == 12:
                img[r][c] = lo
            elif r == 13:
                img[r][c] = hi
            else:
                img[r][c] = face
    for half, v in ((0, a), (1, b)):
        for k in range(9):
            if not (PIPMASK[v] >> k) & 1:
                continue
            across, along = k // 3, k % 3
            x, y = 2 + 3 * across, (2 if half == 0 else 14) + 3 * along
            col = CLASSIC[v] if style == "colour" else pip
            if style in ("drop", "colour"):
                for d in ((2, 1), (1, 2)):
                    img[y + d[1]][x + d[0]] = lo
            elif style == "inset":
                for d in ((2, 0), (2, 1), (2, 2), (1, 2), (0, 2)):
                    img[y + d[1]][x + d[0]] = hi
                for d in ((-1, -1), (0, -1), (1, -1), (-1, 0), (-1, 1)):
                    img[y + d[1]][x + d[0]] = lo if img[y + d[1]][x + d[0]] == face else img[y + d[1]][x + d[0]]
            for dy in range(2):
                for dx in range(2):
                    img[y + dy][x + dx] = col
    return img


def main():
    rows = [("drop", False), ("inset", False), ("colour", False), ("drop", True)]
    pairs = [(1, 3), (2, 5), (6, 6), (4, 0)]
    S = 6
    out = Image.new("RGB", ((len(pairs) * 17 + 3) * S, (len(rows) * 32 + 2) * S), rgb("FELT"))
    for j, (style, dark) in enumerate(rows):
        for i, (a, b) in enumerate(pairs):
            img = tile(a, b, style, dark)
            ox, oy = 3 + i * 17, 2 + j * 32
            for r, row in enumerate(img):            # the drop shadow on the felt
                for c, v in enumerate(row):
                    if v:
                        for yy in range(S):
                            for xx in range(S):
                                out.putpixel(((ox + c + 2) * S + xx, (oy + r + 2) * S + yy), rgb("FELT_DK"))
            for r, row in enumerate(img):
                for c, v in enumerate(row):
                    if v:
                        for yy in range(S):
                            for xx in range(S):
                                out.putpixel(((ox + c) * S + xx, (oy + r) * S + yy), rgb(v))
    (ROOT / "out").mkdir(exist_ok=True)
    out.save(ROOT / "out" / "tilemock.png")
    print("out/tilemock.png: rows = " + ", ".join(f"{s}{' dark' if d else ''}" for s, d in rows))


if __name__ == "__main__":
    main()
