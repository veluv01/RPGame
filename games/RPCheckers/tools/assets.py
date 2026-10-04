"""CHCheckers asset pipeline.

    python tools/assets.py         # build src/assets/*, write previews to build/assets/

Sources (each tools/art/<name>.png if present - hand-finished art - else the
palette-letter text in tools/art/<name>.txt):
  * chip: a man, seen from the table's edge. Its base centre is CHIP_ANCHOR
    below. A king is two of them, the upper one with its crown showing.
  * chiptop: the chip from above (the map view and the HUD's tally).
  * hand: the pointing glove.
  * The palette swap that dresses the chip as each side: tools/art/sides.txt.
  * tools/sheet.py exports all of it as one sprite sheet to edit, and imports
    the edited sheet back into these files.

Outputs:
  src/assets/Assets.h / Assets.cpp   - generated, do not edit
  build/assets/*.png                 - previews on the real palette
"""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ART = HERE / "art"
import sys  # noqa: E402
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))     # the repository's tools/: artlib (shared art in tools/art/common)
import artlib  # noqa: E402
OUT_H = ROOT / "src" / "assets" / "Assets.h"
OUT_C = ROOT / "src" / "assets" / "Assets.cpp"
PREVIEW = ROOT / "build" / "assets"

# Must match pal::HOUSE in the RPGame library
# (platform/board/arduino/RPGame/libraries/RPGame/src/rpgame/Palette.cpp).
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
# Letters used in tools/art/*.txt. ' ' / '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16
SPRITES = ["chip", "chiptop"]
CHIP_ANCHOR = (6, 6)                 # the base centre of the chip
CROWN = 8                            # the art colour of the crown (GOLD)
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]
SIDES = artlib.art(HERE, "sides.txt")
SIDES_HEADER = """# The palette swap that dresses the one chip as each side. The art
# (tools/art/chip.txt, chiptop.txt, or the .png beside them) is drawn in its
# own tones - the MASTER row of tools/sheet.py's sheet - with an INK outline.
# One line per art colour the swap changes: the art colour, then White's
# colour, then Black's (names as in the RPGame library's rpgame/Palette.h).
# Colours not listed stay as drawn. GOLD is the crown: it takes the face's
# colour here, and stays gold on the chip that tops a king.
#
# tools/sheet.py import rewrites this from an edited sheet.
"""


def load_sides():
    """tools/art/sides.txt -> [white, black], each art colour -> side colour (16 entries)."""
    maps = [list(range(16)), list(range(16))]
    for ln in SIDES.read_text().splitlines():
        words = ln.split("#", 1)[0].split()
        if not words:
            continue
        a, w, b = (NAMES.index(n) for n in words)
        maps[0][a], maps[1][a] = w, b
    return maps


def save_sides(maps):
    lines = [f"{NAMES[a]:<8} {NAMES[maps[0][a]]:<8} {NAMES[maps[1][a]]}"
             for a in range(16) if maps[0][a] != a or maps[1][a] != a]
    SIDES.write_text(SIDES_HEADER + "\n".join(lines) + "\n", newline="\n")


def king_maps(maps):
    """The swap for the chip that tops a king: the crown stays as drawn."""
    out = [list(m) for m in maps]
    for m in out:
        m[CROWN] = CROWN
    return out


def load_sprite(name):
    """tools/art/<name>.png if present, else <name>.txt -> rows of palette indices."""
    png = artlib.art(HERE, f"{name}.png")
    return load_png(png) if png.exists() else load_art(name)[0]


def load_hand():
    return load_sprite("hand")


def save_png(path, img):
    """Rows of palette indices -> a palette-exact RGBA PNG (what load_png reads)."""
    h, w = len(img), len(img[0])
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            if img[y][x] != TRANSPARENT:
                im.putpixel((x, y), rgb(img[y][x]) + (255,))
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)


def rgb(i):
    c = PALETTE[i]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


def load_png(path):
    """Palette-exact PNG -> rows of palette indices; alpha 0 is transparent."""
    lut = {rgb(i): i for i in range(15)}          # FX_B (15) shares GOLD's colour
    im = Image.open(path).convert("RGBA")
    rows = []
    for y in range(im.height):
        row = []
        for x in range(im.width):
            r, g, b, a = im.getpixel((x, y))
            if a == 0:
                row.append(TRANSPARENT)
            elif (r, g, b) in lut and a == 255:
                row.append(lut[(r, g, b)])
            else:
                raise SystemExit(f"{path.name} ({x},{y}): #{r:02X}{g:02X}{b:02X} alpha {a} is not a palette colour")
        rows.append(row)
    return rows


def load_art(name):
    """tools/art/<name>.txt: palette letters, one row per line; '#' starts a comment line.
    Several images may follow each other, separated by a blank line."""
    imgs, cur = [], []
    for ln in (artlib.art(HERE, f"{name}.txt")).read_text().splitlines():
        if ln.startswith("#"):
            continue
        if not ln.strip():
            if cur:
                imgs.append(cur)
                cur = []
            continue
        cur.append(ln.rstrip())
    if cur:
        imgs.append(cur)
    out = []
    for rows in imgs:
        w = max(len(r) for r in rows)
        out.append([[TRANSPARENT if ch in " ." else LETTER[ch] for ch in r.ljust(w)] for r in rows])
    return out


def pack_span4(img, trans=TRANSPARENT):
    """Colour image -> w, h, then per row: n, then n bytes of (len-1)<<4 | colour
    (colour 15 = skip). Trailing transparency is implicit."""
    h, w = len(img), len(img[0])
    out = [w, h]
    for row in img:
        runs, x = [], 0
        while x < w:
            c = row[x]
            s = x
            while x < w and row[x] == c and x - s < 16:
                x += 1
            runs.append((x - s, c))
        while runs and runs[-1][1] == trans:
            runs.pop()
        out.append(len(runs))
        for n, c in runs:
            out.append(((n - 1) << 4) | (15 if c == trans else c))
    return out


def preview(name, img, scale=6, bg=3):
    h, w = len(img), len(img[0])
    im = Image.new("RGB", (w, h), rgb(bg))
    for y in range(h):
        for x in range(w):
            if img[y][x] != TRANSPARENT:
                im.putpixel((x, y), rgb(img[y][x]))
    im.resize((w * scale, h * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


def remapped(img, table):
    return [[v if v == TRANSPARENT else table[v] for v in row] for row in img]


def c_array(name, data, per_line=16):
    lines = [f"const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join(str(v) for v in data[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    PREVIEW.mkdir(parents=True, exist_ok=True)
    decls, defs = [], []
    total = 0
    maps = load_sides()
    kings = king_maps(maps)

    # The chip, from the side and from above.
    for name in SPRITES:
        img = load_sprite(name)
        data = pack_span4(img)
        defs.append(c_array("CHIP_TOP" if name == "chiptop" else "CHIP", data))
        total += len(data)
        for side, tag in enumerate(("white", "black")):
            preview(f"{name}_{tag}", remapped(img, maps[side]), 8)
            preview(f"{name}_{tag}_king", remapped(img, kings[side]), 8)
    decls.append("extern const uint8_t CHIP[];                                 // span4: a man, from the table's edge\n"
                 f"constexpr int8_t CHIP_AX = {CHIP_ANCHOR[0]}, CHIP_AY = {CHIP_ANCHOR[1]};                    // its base centre\n"
                 "extern const uint8_t CHIP_TOP[];                             // span4: from above")

    # Each side's colours for the art; the chip that tops a king keeps its crown.
    for cname, tab in (("SIDE_REMAP", maps), ("KING_REMAP", kings)):
        defs.append(f"const uint8_t {cname}[2][16] = {{\n" +
                    "".join("    {" + ", ".join(str(v) for v in m) + "},\n" for m in tab) + "};")
        total += 32
    decls.append("extern const uint8_t SIDE_REMAP[2][16];                      // art colour -> White's, Black's (tools/art/sides.txt)\n"
                 "extern const uint8_t KING_REMAP[2][16];                      // ... with the crown showing")

    # The pointing hand (span4).
    hand = load_hand()
    data = pack_span4(hand)
    defs.append(c_array("HAND", data))
    tip = [x for x, v in enumerate(hand[-1]) if v != TRANSPARENT]
    decls.append("extern const uint8_t HAND[];                                 // span4, fingertip on the bottom row\n"
                 f"constexpr uint8_t HAND_TIP = {(tip[0] + tip[-1]) // 2};                           // its column")
    total += len(data)
    preview("hand", hand, 8)

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("// Generated by tools/assets.py - do not edit.\n#pragma once\n#include <stdint.h>\n\n"
                     + "\n".join(decls) + "\n", newline="\n")
    OUT_C.write_text("// Generated by tools/assets.py - do not edit.\n"
                     "// Art from tools/art/ (text, or the .png beside it where there is one).\n"
                     "#include \"Assets.h\"\n\n" + "\n\n".join(defs) + "\n", newline="\n")
    print(f"assets: {total} bytes of data")


if __name__ == "__main__":
    main()
