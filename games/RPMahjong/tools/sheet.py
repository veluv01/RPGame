"""The EASY tile faces (tools/art/tiles.txt) as one picture, for editing in a
paint program. (The CLASSIC faces come from tools/faces.py.)

    python tools/sheet.py export [SHEET.png]     tools/art/tiles.txt -> a sheet (default tools/art/sheet.png)
    python tools/sheet.py import [SHEET.png]     the sheet -> tools/art/tiles.txt, then tools/assets.py

The sheet is an indexed PNG on the game's palette: every face 7 x 11 on a
white tile, nine to a row in the game's order (dots, bamboo, characters,
winds and dragons, flowers and seasons, the back), a pixel of felt between
them, and the palette along the bottom to pick colours from.

Draw in the palette's colours only. White is the tile itself; a face may use
two other colours, and not the felt greens or the two animated colours (the
import says which face breaks a rule). Photoshop may reorder the colour
table when it saves: the import goes by each pixel's colour, not its index.
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image

import assets

HERE = Path(__file__).resolve().parent
ART = HERE / "art"
SHEET = ART / "sheet.png"
PER_ROW = 9
PITCH_X, PITCH_Y = assets.FACE_W + 1, assets.FACE_H + 1
FELT, WHITE = 3, 1
LETTER_OF = {v: k for k, v in assets.LETTER.items()}

# tiles.txt as tools/assets.py reads it: a title and how many faces on the row.
ROWS = [("dots", 9), ("bamboo", 9), ("chars", 9), ("winds: east south west north", 4),
        ("dragons: red green white", 3), ("flowers (any two match)", 4),
        ("seasons (any two match)", 4), ("the back of a tile", 1)]
HEADER = """# The tile faces, 7 x 11 each, in the order the game numbers them (0-41, then
# the back). A letter is a palette colour, a dot is the tile's own face:
#   k ink  r red  u blue  g green (FELT_LT)  y gold  b wood  n navy  c cyan
# A face may use two colours at most. Faces on a row are a space apart;
# python tools/assets.py turns this into src/assets/Assets.cpp.
# (python tools/sheet.py export / import: the same faces as a picture.)
"""


def cell_origin(k):
    return 1 + (k % PER_ROW) * PITCH_X, 1 + (k // PER_ROW) * PITCH_Y


def export(path):
    faces = assets.load_tiles()
    rows = (len(faces) + PER_ROW - 1) // PER_ROW
    w, h = PER_ROW * PITCH_X + 1, rows * PITCH_Y + 1 + 5
    im = Image.new("P", (w, h), FELT)
    pal = []
    for i in range(16):
        pal += list(assets.rgb(i))
    im.putpalette(pal + [0] * (768 - len(pal)))
    for k, face in enumerate(faces):
        x0, y0 = cell_origin(k)
        for y in range(assets.FACE_H):
            for x in range(assets.FACE_W):
                c = face[y][x]
                im.putpixel((x0 + x, y0 + y), WHITE if c == assets.TRANSPARENT else c)
    # The palette to pick from (the two animated colours left out).
    for i in range(14):
        for y in range(4):
            for x in range(4):
                im.putpixel((1 + i * 5 + x, h - 5 + y), i)
    im.save(path)
    print(f"{path}: {len(faces)} faces")


def import_(path):
    im = Image.open(path).convert("RGB")
    lut = {assets.rgb(i): i for i in range(15)}
    faces = []
    for k in range(assets.FACES + 1):
        x0, y0 = cell_origin(k)
        face = []
        for y in range(assets.FACE_H):
            row = ""
            for x in range(assets.FACE_W):
                px = im.getpixel((x0 + x, y0 + y))
                if px not in lut:
                    raise SystemExit(f"face {k} at ({x},{y}): #{px[0]:02X}{px[1]:02X}{px[2]:02X} is not a palette colour")
                c = lut[px]
                row += "." if c == WHITE else LETTER_OF[c]
            face.append(row)
        faces.append(face)
    out, k = [HEADER], 0
    for title, n in ROWS:
        out.append(f"# {title}")
        for r in range(assets.FACE_H):
            out.append(" ".join(faces[k + i][r] for i in range(n)))
        out.append("")
        k += n
    (ART / "tiles.txt").write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(f"tools/art/tiles.txt: {len(faces)} faces from {path}")
    raise SystemExit(subprocess.run([sys.executable, str(HERE / "assets.py")]).returncode)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("export", "import"):
        raise SystemExit(__doc__)
    path = Path(sys.argv[2]) if len(sys.argv) > 2 else SHEET
    (export if sys.argv[1] == "export" else import_)(path)


if __name__ == "__main__":
    main()
