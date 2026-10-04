"""CHDominoes asset pipeline.

    python tools/assets.py         # build src/assets/*, write previews to build/assets/

Sources, all in tools/art/:
  * hand.png, else hand.txt: the pointing glove (CHChess's).
  * aafont.txt: the serif lettering, anti-aliased (from tools/aafont.py).
  * logo.txt: the title's name at its full size, letter by letter.

The tiles themselves are drawn by the game (Table.cpp): there is
no art for them here.

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
GEN = ART / "gen"
OUT_H = ROOT / "src" / "assets" / "Assets.h"
OUT_C = ROOT / "src" / "assets" / "Assets.cpp"
PREVIEW = ROOT / "build" / "assets"

# Must match COLOURS in Colours.cpp.
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xEEE, 0x445, 0xF0F, 0xFC2]
# Letters used in tools/art/*.txt. ' ' / '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "BONE", "SLATE", "FX_A", "FX_B"]


def rgb(i):
    c = PALETTE[i]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


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


def source(name):
    """tools/art/<name>.png if someone has drawn one, else the letters in <name>.txt."""
    png = artlib.art(HERE, f"{name}.png")
    return load_png(png) if png.exists() else load_art(name)[0]


def load_logo(word="Dominoes", gap=2):
    """tools/art/logo.txt -> rows of 0/1: the word's letters side by side."""
    glyphs, cur = {}, None
    for ln in (artlib.art(HERE, "logo.txt")).read_text().splitlines():
        if not ln.strip() or ln.startswith("# ") or ln == "#":     # (rows start with '#' too)
            continue
        if ln.startswith("= "):
            parts = ln[2:].split()
            cur = {"top": int(parts[1]) if len(parts) > 1 else 0, "rows": []}
            glyphs[parts[0]] = cur
            continue
        cur["rows"].append([1 if ch == "#" else 0 for ch in ln.rstrip()])
    h = max(g["top"] + len(g["rows"]) for g in glyphs.values())
    w = sum(len(glyphs[c]["rows"][0]) for c in word) + gap * (len(word) - 1)
    img = [[0] * w for _ in range(h)]
    x = 0
    for c in word:
        g = glyphs[c]
        for j, row in enumerate(g["rows"]):
            for i, v in enumerate(row):
                img[g["top"] + j][x + i] |= v
        x += len(g["rows"][0]) + gap
    return img


def pack_1bpp(img):
    """Rows of 0/1 -> MSB-first bytes, each row whole bytes."""
    out = []
    for row in img:
        for i in range(0, len(row), 8):
            chunk = row[i:i + 8] + [0] * (8 - len(row[i:i + 8]))
            out.append(sum(b << (7 - k) for k, b in enumerate(chunk)))
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


def remapped(img, table):
    return [[v if v == TRANSPARENT else table[v] for v in row] for row in img]


def preview(name, imgs, scale=8, bg=3):
    """Images side by side on the felt."""
    h = max(len(i) for i in imgs)
    w = sum(len(i[0]) + 2 for i in imgs) + 2
    im = Image.new("RGB", (w, h + 4), rgb(bg))
    x0 = 2
    for img in imgs:
        for y in range(len(img)):
            for x in range(len(img[0])):
                if img[y][x] != TRANSPARENT:
                    im.putpixel((x0 + x, 2 + y), rgb(img[y][x]))
        x0 += len(img[0]) + 2
    im.resize((w * scale, (h + 4) * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


def c_array(name, data, per_line=20):
    lines = [f"const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join(str(v) for v in data[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    PREVIEW.mkdir(parents=True, exist_ok=True)
    decls, defs = [], []
    total = 0
    # The pointing hand (span4).
    hand = source("hand")
    data = pack_span4(hand)
    defs.append(c_array("HAND", data))
    tip = [x for x, v in enumerate(hand[-1]) if v != TRANSPARENT]
    # (Turned over, for pointing at the far side from below, by drawing it
    # upside down, sprite4's SPR_FLIP_V: no second copy.)
    decls.append(f"extern const uint8_t HAND[{len(data)}];                            // span4, fingertip on the bottom row\n"
                 f"constexpr uint8_t HAND_TIP = {(tip[0] + tip[-1]) // 2};                           // its column")
    total += len(data)
    preview("hand", [hand, hand[::-1]])

    # The serif lettering.
    glyphs, cur = {}, None
    for ln in (artlib.art(HERE, "aafont.txt")).read_text().splitlines():
        if ln.startswith("= "):
            cur = glyphs.setdefault(ln[2], [])
        elif ln.strip() and not ln.startswith("# "):
            cur.append(ln.strip())
    words, height = [], 0
    for ch, rows in glyphs.items():
        w = max(len(r) for r in rows)
        assert w <= 16, ch
        height = max(height, len(rows))
        words += [ord(ch) << 8 | w, len(rows)]
        for mark in "#+":
            words += [sum(1 << (15 - i) for i, v in enumerate(r) if v == mark) for r in rows]
    words.append(0)
    defs.append(f"const uint16_t AAFONT[{len(words)}] = {{\n" +
                "".join("    " + ", ".join(f"0x{v:04X}" for v in words[i:i + 12]) + ",\n"
                        for i in range(0, len(words), 12)) + "};")
    decls.append(f"extern const uint16_t AAFONT[{len(words)}];                     // the serif lettering (tools/art/aafont.txt): per glyph\n"
                 "                                                            // char << 8 | width, rows, the rows of ink, the rows of half ink\n"
                 f"constexpr uint8_t AAFONT_H = {height};")
    total += 2 * len(words)

    # The title's name.
    logo = load_logo()
    data = pack_1bpp(logo)
    defs.append(c_array("LOGO", data))
    decls.append(f"extern const uint8_t LOGO[{len(data)}];                         // the title's name (tools/art/logo.txt), 1 bpp rows\n"
                 f"constexpr uint8_t LOGO_W = {len(logo[0])}, LOGO_H = {len(logo)};")
    total += len(data)
    preview("logo", [[[1 if v else TRANSPARENT for v in row] for row in logo]], scale=6, bg=0)

    # The palette, for whoever edits the art.
    sw = Image.new("RGB", (16 * 24, 24))
    for i in range(16):
        for y in range(24):
            for x in range(24):
                sw.putpixel((i * 24 + x, y), rgb(i))
    sw.save(PREVIEW / "palette.png")

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("// Generated by tools/assets.py - do not edit.\n#pragma once\n#include <stdint.h>\n\n"
                     + "\n".join(decls) + "\n", newline="\n")
    OUT_C.write_text("// Generated by tools/assets.py - do not edit.\n"
                     "// The art is in tools/art/ (see tools/assets.py for how to redraw it).\n"
                     "#include \"Assets.h\"\n\n" + "\n\n".join(defs) + "\n", newline="\n")
    print(f"assets: {total} bytes of data")


if __name__ == "__main__":
    main()
