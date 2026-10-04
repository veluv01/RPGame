"""Write the CLASSIC tile faces: tools/art/classic.txt (7 x 11, the whole table)
and tools/art/classic2x.txt (15 x 23, the close-up).

    python tools/faces.py

The dots and bamboo are laid out here from their traditional patterns; the
characters, winds, dragons and the bird are drawn below as text. The two
.txt files are what tools/assets.py packs, so they can also be finished by
hand afterwards (running this again overwrites them).

Letters: k ink, r red, u blue, n navy, g green (FELT_LT), y gold, '.' the
face. A face may use two colours (the emboss is added by tools/assets.py).
"""
from pathlib import Path

ART = Path(__file__).resolve().parent / "art"


def blank(w, h):
    return [["."] * w for _ in range(h)]


def stamp(img, art, x, y, recolour=None):
    for j, row in enumerate(art):
        for i, ch in enumerate(row):
            if ch != ".":
                img[y + j][x + i] = recolour.get(ch, ch) if recolour else ch


def centred(art, w, h, dy=0):
    img = blank(w, h)
    stamp(img, art, (w - len(art[0])) // 2, (h - len(art)) // 2 + dy)
    return img


def recolour(art, a, b):
    return [row.replace(a, b) for row in art]


# ---------------------------------------------------------------------------
# 7 x 11
# ---------------------------------------------------------------------------
NUM_S = {  # Chinese numerals, 7 x 5
    1: [".......", ".......", "nnnnnnn", ".......", "......."],
    2: [".......", ".nnnnn.", ".......", "nnnnnnn", "......."],
    3: [".nnnnn.", ".......", "..nnn..", ".......", "nnnnnnn"],
    4: ["nnnnnnn", "n.n.n.n", "n.n.n.n", "nn...nn", "nnnnnnn"],
    5: ["nnnnnnn", "..n....", ".nnnnn.", ".n...n.", "nnnnnnn"],
    6: ["...n...", "nnnnnnn", ".......", ".n...n.", "n.....n"],
    7: ["..n....", "..n..nn", "nnnnn..", "..n....", "..nnnnn"],
    8: ["..n.n..", "..n.n..", ".n...n.", ".n...n.", "n.....n"],
    9: [".n.....", "nnnn...", ".n..n..", ".n..n.n", "n...nnn"],
}
WAN_S = ["rrrrrrr", "..r....", "..rrrr.", ".r...r.", "r...rr."]

BEAD3 = ["uuu", "uru", "uuu"]
DOT1_S = ["..uuu..", ".u...u.", "u.rrr.u", "u.r.r.u", "u.rrr.u", ".u...u.", "..uuu.."]


def dots_s(n):
    img = blank(7, 11)
    if n == 1:
        stamp(img, DOT1_S, 0, 2)
        return img
    if n <= 6:
        at = {2: [(2, 1), (2, 7)], 3: [(0, 0), (2, 4), (4, 8)], 4: [(0, 1), (4, 1), (0, 7), (4, 7)],
              5: [(0, 0), (4, 0), (2, 4), (0, 8), (4, 8)],
              6: [(0, 0), (4, 0), (0, 4), (4, 4), (0, 8), (4, 8)]}[n]
        for k, (x, y) in enumerate(at):
            red = (n == 5 and k == 2) or (n == 6 and k >= 2)
            stamp(img, recolour(recolour(BEAD3, "u", "x"), "r", "u") if red else BEAD3, x, y,
                  {"x": "r"} if red else None)
        return img
    if n == 7:
        for x, y in [(0, 0), (3, 1), (5, 3)]:
            stamp(img, ["uu", "uu"], x, y)
        for x, y in [(1, 6), (4, 6), (1, 9), (4, 9)]:
            stamp(img, ["rr", "rr"], x, y)
        return img
    if n == 8:
        for y in (0, 3, 6, 9):
            for x in (1, 4):
                stamp(img, ["uu", "uu"], x, y)
        return img
    for j, y in enumerate((0, 4, 8)):
        for x in (1, 3, 5):
            stamp(img, ["r" if j == 1 else "u"] * 2, x, y + 1)
    return img


def stick(h, c="g"):
    """A bamboo stick 1 wide."""
    return [c] * h


def bamboo_s(n):
    img = blank(7, 11)
    if n == 1:
        return [list(r) for r in BIRD_S]
    lay = {  # (x, y, length, red)
        2: [(3, 0, 5, 0), (3, 6, 5, 0)],
        3: [(3, 0, 5, 0), (1, 6, 5, 0), (5, 6, 5, 0)],
        4: [(1, 0, 5, 0), (5, 0, 5, 0), (1, 6, 5, 0), (5, 6, 5, 0)],
        5: [(1, 0, 5, 0), (5, 0, 5, 0), (3, 3, 5, 1), (1, 6, 5, 0), (5, 6, 5, 0)],
        6: [(x, y, 5, 0) for y in (0, 6) for x in (1, 3, 5)],
        7: [(3, 0, 3, 1)] + [(x, y, 3, 0) for y in (4, 8) for x in (1, 3, 5)],
        8: [(x, y, 5, 0) for y in (0, 6) for x in (0, 2, 4, 6)],
        9: [(x, y, 3, x == 3) for y in (0, 4, 8) for x in (1, 3, 5)],
    }[n]
    for x, y, h, red in lay:
        stamp(img, stick(h, "r" if red else "g"), x, y)
    return img


BIRD_S = ["..rr...", ".rgrr..", ".ggg...", "gggg...", ".gggg..", "..ggggg", "...gggg",
          "...g.g.", "..g..g.", ".......", "......."]

WINDS_S = [
    ["...k...", "kkkkkkk", ".kkkkk.", ".k.k.k.", ".kkkkk.", ".k.k.k.", ".kkkkk.", "...k...",
     "..kkk..", ".k.k.k.", "k..k..k"],                                                    # east
    ["...k...", "kkkkkkk", "...k...", "kkkkkkk", "k.....k", "kk...kk", "k.kkk.k", "k..k..k",
     "k.kkk.k", "k..k..k", "k..k.kk"],                                                    # south
    [".......", "kkkkkkk", "..k.k..", "kkkkkkk", "k.k.k.k", "k.k.k.k", "kk...kk", "k.....k",
     "k.....k", "kkkkkkk", "......."],                                                    # west
    ["..k.k..", "..k.k..", "..k.k.k", "kkk.kk.", "..k.k..", "..k.k..", "..k.k..", "..k.k..",
     "kkk.k..", "....k.k", "....kkk"],                                                    # north
]
DRAGONS_S = [
    ["...r...", "...r...", "rrrrrrr", "r..r..r", "r..r..r", "rrrrrrr", "...r...", "...r...",
     "...r...", "...r...", "......."],                                                    # red: 中
    [".g.g.g.", "ggggggg", "...g...", "ggggggg", "g.....g", "ggggggg", "..g.g..", ".g...g.",
     "ggggggg", "...g...", "..g.g.."],                                                    # green: 發
    [".......", "uuuuuuu", "u.....u", "u.uuu.u", "u.u.u.u", "u.u.u.u", "u.u.u.u", "u.uuu.u",
     "u.....u", "uuuuuuu", "......."],                                                    # white
]

# ---------------------------------------------------------------------------
# 15 x 23
# ---------------------------------------------------------------------------
NUM_L = {  # 13 x 9, two-pixel strokes
    1: [".............", ".............", ".............", "...........n.", "nnnnnnnnnnnnn",
        ".nnnnnnnnnnn.", ".............", ".............", "............."],
    2: [".............", "...nnnnnnn...", "...nnnnnnn...", ".............", ".............",
        ".............", "nnnnnnnnnnnnn", "nnnnnnnnnnnnn", "............."],
    3: ["..nnnnnnnnn..", "..nnnnnnnnn..", ".............", "...nnnnnnn...", "...nnnnnnn...",
        ".............", ".............", "nnnnnnnnnnnnn", "nnnnnnnnnnnnn"],
    4: ["nnnnnnnnnnnnn", "nn...n.n...nn", "nn...n.n...nn", "nn..nn.n...nn", "nn.nn..nn..nn",
        "nnnn....nnnnn", "nn.........nn", "nnnnnnnnnnnnn", "nnnnnnnnnnnnn"],
    5: [".nnnnnnnnnnn.", ".....nn......", ".....nn......", "..nnnnnnnnn..", "....nn....nn.",
        "....nn....nn.", "...nn.....nn.", "nnnnnnnnnnnnn", "nnnnnnnnnnnnn"],
    6: ["......nn.....", ".......nn....", "nnnnnnnnnnnnn", "nnnnnnnnnnnnn", ".............",
        "...nn....nn..", "..nn......nn.", ".nn........nn", "nn..........n"],
    7: ["....nn.......", "....nn....nnn", "nnnnnnnnnnn..", "nnnnnn.......", "....nn.......",
        "....nn.......", "....nn.....nn", "....nn.....nn", ".....nnnnnnnn"],
    8: [".....n..n....", "....nn..nn...", "....nn...nn..", "...nn.....nn.", "...nn.....nn.",
        "..nn.......nn", ".nn.........n", "nn...........", "............."],
    9: ["...nn........", "...nn........", "nnnnnnnnn....", "nnnnnnnnn....", "...nn...nn...",
        "...nn...nn...", "..nn....nn..n", ".nn.....nn..n", "nn......nnnnn"],
}
WAN_L = ["...r.....r...", "rrrrrrrrrrrrr", "...r.....r...", ".rrrrrrrrrrr.", ".r....r....r.",
         ".rrrrrrrrrrr.", ".r....r....r.", ".rrrrrrrrrrr.", "r.....r.....r", "r...rr.rr...r",
         "r..r.....r.rr"]

BEAD5 = [".uuu.", "u.r.u", "urrru", "u.r.u", ".uuu."]
BEAD4 = [".uu.", "urru", "urru", ".uu."]
DOT1_L = ["....uuuuu....", "..uu.....uu..", ".u...rrr...u.", ".u..r...r..u.", "u..r..u..r..u",
          "u.r..uuu..r.u", "u.r.uuruu.r.u", "u.r..uuu..r.u", "u..r..u..r..u", ".u..r...r..u.",
          ".u...rrr...u.", "..uu.....uu..", "....uuuuu...."]


def swap(art):
    """Blue <-> red."""
    return [row.replace("u", "x").replace("r", "u").replace("x", "r") for row in art]


def dots_l(n):
    img = blank(15, 23)
    if n == 1:
        stamp(img, DOT1_L, 1, 5)
        return img
    if n <= 6:
        at = {2: [(5, 3), (5, 15)], 3: [(1, 1), (5, 9), (9, 17)], 4: [(2, 3), (8, 3), (2, 15), (8, 15)],
              5: [(1, 2), (9, 2), (5, 9), (1, 16), (9, 16)],
              6: [(2, 1), (8, 1), (2, 9), (8, 9), (2, 17), (8, 17)]}[n]
        for k, (x, y) in enumerate(at):
            red = (n == 5 and k == 2) or (n == 6 and k >= 2) or (n == 3 and k == 1)
            stamp(img, swap(BEAD5) if red else BEAD5, x, y)
        return img
    if n == 7:
        for x, y in [(0, 0), (5, 2), (10, 4)]:
            stamp(img, BEAD4, x, y)
        for x, y in [(2, 11), (9, 11), (2, 17), (9, 17)]:
            stamp(img, swap(BEAD4), x, y)
        return img
    if n == 8:
        for y in (1, 7, 13, 19):
            for x in (3, 8):
                stamp(img, BEAD4, x, y)
        return img
    for j, y in enumerate((2, 9, 16)):
        for x in (0, 5, 10):
            stamp(img, swap(BEAD4) if j == 1 else BEAD4, x, y)
    return img


def stick_l(h, c="g"):
    """A bamboo stick 3 wide: knots at the ends (and the middle of a long one)."""
    rows = []
    for j in range(h):
        knot = j in (0, h - 1) or (h >= 9 and j == h // 2)
        rows.append(c * 3 if knot else "." + c + ".")
    return rows


def bamboo_l(n):
    img = blank(15, 23)
    if n == 1:
        stamp(img, BIRD_L, 0, 2)
        return img
    lay = {  # (x, y, length, red)
        2: [(6, 0, 11, 0), (6, 12, 11, 0)],
        3: [(6, 0, 11, 0), (2, 12, 11, 0), (10, 12, 11, 0)],
        4: [(3, 0, 11, 0), (9, 0, 11, 0), (3, 12, 11, 0), (9, 12, 11, 0)],
        5: [(1, 0, 11, 0), (11, 0, 11, 0), (6, 6, 11, 1), (1, 12, 11, 0), (11, 12, 11, 0)],
        6: [(x, y, 11, 0) for y in (0, 12) for x in (1, 6, 11)],
        7: [(6, 0, 7, 1)] + [(x, y, 7, 0) for y in (8, 16) for x in (1, 6, 11)],
        8: [(x, y, 11, 0) for y in (0, 12) for x in (0, 4, 8, 12)],
        9: [(x, y, 7, x == 6) for y in (0, 8, 16) for x in (1, 6, 11)],
    }[n]
    for x, y, h, red in lay:
        stamp(img, stick_l(h, "r" if red else "g"), x, y)
    return img


BIRD_L = [".......rr......", "......rrrr.....", "......rgrr.....", ".....gggg......", "....gggggg.....",
          "...ggggggg..r..", "..gggggggg.rr..", ".ggg.gggggrrr..", "gg...ggggrrr...", "g....gggggg....",
          ".....g.ggg.....", "....g..g.g.....", "...g..g...g....", "..g..g.....g...", "....g..........",
          "...............", "..ggggggggggg..", "...............", "..............."]

WINDS_L = [
    ["......k......", "......k......", "kkkkkkkkkkkkk", "......k......", ".kkkkkkkkkkk.", ".k....k....k.",
     ".kkkkkkkkkkk.", ".k....k....k.", ".kkkkkkkkkkk.", "......k......", ".....kkk.....", "....k.k.k....",
     "...k..k..k...", "..k...k...k..", ".k....k....k.", "k.....k.....k", "......k......"],
    ["......k......", "......k......", "kkkkkkkkkkkkk", "......k......", "......k......", "kkkkkkkkkkkkk",
     "kk.........kk", "kk..k...k..kk", "kk...k.k...kk", "kk.kkkkkkk.kk", "kk....k....kk", "kk.kkkkkkk.kk",
     "kk....k....kk", "kk....k....kk", "kk....k..kkkk", "kk.......kkk."],
    ["kkkkkkkkkkkkk", "kkkkkkkkkkkkk", "....kk.kk....", "....kk.kk....", "kkkkkkkkkkkkk", "kk..kk.kk..kk",
     "kk..kk.kk..kk", "kk..kk.kk..kk", "kk.kk...kk.kk", "kkkk.....kkkk", "kk.........kk", "kk.........kk",
     "kkkkkkkkkkkkk", "kkkkkkkkkkkkk"],
    ["....kk..kk...", "....kk..kk...", "....kk..kk..k", "....kk..kk.kk", "kkkkkk..kkkk.", "....kk..kkk..",
     "....kk..kk...", "....kk..kk...", "....kk..kk...", "...kkk..kk...", ".kk.kk..kk..k", "....kk..kk..k",
     "....kk..kk..k", "....kk...kkkk"],
]
DRAGONS_L = [
    ["......r......", "......r......", "......r......", "rrrrrrrrrrrrr", "rrrrrrrrrrrrr", "rr....r....rr",
     "rr....r....rr", "rr....r....rr", "rr....r....rr", "rrrrrrrrrrrrr", "rrrrrrrrrrrrr", "......r......",
     "......r......", "......r......", "......r......", "......r......", "......r......"],
    ["....g.....g....", ".gggg..g..gggg.", "...g..g.g..g...", "..g.gg...gg.g..", ".g...........g.",
     "ggggg..gggggg..", "....g..g...g...", "gggg...gg.gg...", "g......g.g.g...", "gggg..g...gg...",
     "...g..ggggggg..", "...g..g.g...g..", "..g...g..g.g...", "gg...g....g....", "....g....g.g...",
     "...g....g...gg."],
    ["uuuuuuuuuuuuu", "u...........u", "u.uuuuuuuuu.u", "u.u.......u.u", "u.u.......u.u", "u.u.......u.u",
     "u.u.......u.u", "u.u.......u.u", "u.u.......u.u", "u.u.......u.u", "u.u.......u.u", "u.u.......u.u",
     "u.u.......u.u", "u.u.......u.u", "u.u.......u.u", "u.uuuuuuuuu.u", "u...........u", "uuuuuuuuuuuuu"],
]

# Flowers and seasons (both sizes): the small pictures from tiles.txt, and
# twice their size for the close-up.
FLOWERS_S = [
    ["...r...", "..rrr..", ".rr.rr.", "..rrr..", "...r...", "...g...", ".g.g...", "..gg.g.", "...gg..", "...g...", "...g..."],
    [".r...r.", "..r.r..", ".rr.rr.", "..r.r..", ".r.g.r.", "...g...", "...g.g.", ".g.gg..", "..gg...", "...g...", "...g..."],
    ["..r.r..", ".rrrrr.", "..r.r..", ".rrrrr.", "..r.r..", "...g...", ".g.g...", "..gg.g.", "...gg..", "...g...", "...g..."],
    ["..rrr..", ".r.r.r.", ".rrrrr.", ".r.r.r.", "..rrr..", "...g...", "...g.g.", ".g.gg..", "..gg...", "...g...", "...g..."],
]
SEASONS_S = [
    [".......", "...u...", "..uuu..", ".uuyuu.", "..uuu..", "...u...", ".u.u...", "..uu.u.", "...uu..", "...u...", "...u..."],
    [".......", "u..u..u", ".u.u.u.", "..yyy..", "uuyyyuu", "..yyy..", ".u.u.u.", "u..u..u", ".......", ".......", "......."],
    [".......", "...u...", "..uyu..", ".uyyyu.", ".uyuyu.", ".uyyyu.", "..uyu..", "...u...", "...u...", "..u....", "......."],
    [".......", "...u...", ".u.u.u.", "..uuu..", "uuuyuuu", "..uuu..", ".u.u.u.", "...u...", ".......", ".......", "......."],
]
BACK_S = ["ggggggg", "gyggygg", "ggyggyg", "gggyggy", "yggyggg", "gyggygg", "ggyggyg", "gggyggy", "yggyggg",
          "gyggygg", "ggggggg"]


def double(art):
    out = []
    for row in art:
        r = "".join(ch * 2 for ch in row)
        out += [r, r]
    return out


def suit_s(n, ink):
    img = blank(7, 11)
    stamp(img, NUM_S[n], 0, 0)
    stamp(img, WAN_S, 0, 6)
    return img


def chars_l(n):
    img = blank(15, 23)
    stamp(img, NUM_L[n], 1, 1)
    stamp(img, WAN_L, 1, 11)
    return img


def faces_small():
    out = [("dots", [dots_s(n) for n in range(1, 10)]),
           ("bamboo", [bamboo_s(n) for n in range(1, 10)]),
           ("characters", [suit_s(n, "n") for n in range(1, 10)]),
           ("winds: east south west north", [centred(w, 7, 11) for w in WINDS_S]),
           ("dragons: red green white", [centred(d, 7, 11) for d in DRAGONS_S]),
           ("flowers (any two match)", [centred(f, 7, 11) for f in FLOWERS_S]),
           ("seasons (any two match)", [centred(f, 7, 11) for f in SEASONS_S]),
           ("the back of a tile", [centred(BACK_S, 7, 11)])]
    return out


def faces_large():
    out = [("dots", [dots_l(n) for n in range(1, 10)]),
           ("bamboo", [bamboo_l(n) for n in range(1, 10)]),
           ("characters", [chars_l(n) for n in range(1, 10)]),
           ("winds: east south west north", [centred(w, 15, 23) for w in WINDS_L]),
           ("dragons: red green white", [centred(d, 15, 23) for d in DRAGONS_L]),
           ("flowers (any two match)", [centred(double(f), 15, 23) for f in FLOWERS_S]),
           ("seasons (any two match)", [centred(double(f), 15, 23) for f in SEASONS_S]),
           ("the back of a tile", [centred(double(BACK_S), 15, 23)])]
    return out


def write(path, groups, w, h, what):
    lines = [f"# The CLASSIC tile faces, {w} x {h} each ({what}), in the order the game",
             "# numbers them (0-41, then the back). A letter is a palette colour, a dot",
             "# is the tile's own face:",
             "#   k ink  r red  u blue  n navy  g green (FELT_LT)  y gold  b wood  c cyan",
             "# A face may use two colours at most. Faces on a row are a space apart.",
             "# Written by python tools/faces.py; python tools/assets.py packs it.", ""]
    for title, faces in groups:
        lines.append(f"# {title}")
        for f in faces:
            assert len(f) == h and all(len(r) == w for r in f), (title, len(f), [len(r) for r in f])
        for r in range(h):
            lines.append(" ".join("".join(f[r]) for f in faces))
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main():
    write(ART / "classic.txt", faces_small(), 7, 11, "the whole table")
    write(ART / "classic2x.txt", faces_large(), 15, 23, "the close-up")
    print("tools/art/classic.txt, tools/art/classic2x.txt")


if __name__ == "__main__":
    main()
