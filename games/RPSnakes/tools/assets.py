"""SNAKES & LADDERS asset pipeline.

    python tools/assets.py         # build src/assets/*, write previews to build/assets/

Sources:
  * tools/art/sprites.txt: every sprite as palette letters.
  * tools/art/<name>.png (lower case), if present, replaces the sprite of
    that name: palette-exact art, as tools/sheet.py import writes from an
    edited sheet.

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
# Letters used in tools/art/sprites.txt. '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16

# The table the game indexes: tokens by seat (see Stage.cpp).
TOKENS = ["TOKEN_CHERRIES", "TOKEN_BANANA", "TOKEN_APPLE", "TOKEN_STRAWBERRY"]
# Stored a bit a pixel (rows, MSB first), not as a colour sprite.
MONO = ["LOGO_SNAKES", "LOGO_LADDERS"]
# ... and of those, drawn at half size: each pixel becomes four.
DOUBLED = ["LOGO_SNAKES"]


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


def load_sprites():
    """tools/art/sprites.txt (and any PNGs that replace its sprites) -> {name: rows}, in file order."""
    out, name, rows = {}, None, []

    def flush():
        if name:
            w = max(len(r) for r in rows)
            out[name] = [[TRANSPARENT if ch == "." else LETTER[ch] for ch in r.ljust(w, ".")] for r in rows]

    for ln in (artlib.art(HERE, "sprites.txt")).read_text().splitlines():
        ln = ln.rstrip()
        if ln.startswith("#") or not ln:
            continue
        if ln.startswith("@"):
            flush()
            name, rows = ln[1:].strip(), []
        else:
            rows.append(ln)
    flush()
    for name in out:
        png = artlib.art(HERE, f"{name.lower()}.png")
        if png.exists():
            out[name] = load_png(png)
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


def pack_rows1(img):
    """1-bit rows, MSB first (any opaque pixel = 1)."""
    out = []
    for row in img:
        for x0 in range(0, len(row), 8):
            b = 0
            for i, v in enumerate(row[x0:x0 + 8]):
                if v != TRANSPARENT:
                    b |= 0x80 >> i
            out.append(b)
    return out


def preview(name, img, scale=8, bg=1):
    h, w = len(img), len(img[0])
    im = Image.new("RGB", (w, h), rgb(bg))
    for y in range(h):
        for x in range(w):
            if img[y][x] != TRANSPARENT:
                im.putpixel((x, y), rgb(img[y][x]))
    im.resize((w * scale, h * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


def c_array(name, data, per_line=16):
    lines = [f"const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join(str(v) for v in data[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    PREVIEW.mkdir(parents=True, exist_ok=True)
    sprites = load_sprites()
    decls, defs = [], []
    total = 0
    for name, img in sprites.items():
        if name in DOUBLED:
            img = [[v for v in row for _ in (0, 1)] for row in img for _ in (0, 1)]
        data = pack_rows1(img) if name in MONO else pack_span4(img)
        defs.append(c_array(name, data))
        decls.append(f"extern const uint8_t {name}[{len(data)}];")
        if name in MONO:
            decls.append(f"constexpr uint8_t {name}_W = {len(img[0])}, {name}_H = {len(img)};")
        total += len(data)
        preview(name.lower(), img)

    defs.append("const uint8_t *const TOKEN[4] = {" + ", ".join(TOKENS) + "};")
    decls.append("extern const uint8_t *const TOKEN[4];        // by seat")
    total += 16

    hand = sprites["HAND"]
    tip = [x for x, v in enumerate(hand[-1]) if v != TRANSPARENT]
    decls.append(f"constexpr uint8_t HAND_TIP = {(tip[0] + tip[-1]) // 2};          // the fingertip's column (bottom row)")

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("// Generated by tools/assets.py - do not edit.\n"
                     "// Sprites in the span4 format (the RPGame library's rpgame/Draw.h).\n"
                     "#pragma once\n#include <stdint.h>\n\n"
                     + "\n".join(decls) + "\n", newline="\n")
    OUT_C.write_text("// Generated by tools/assets.py - do not edit.\n"
                     "// From tools/art/sprites.txt (and any tools/art/*.png drawn over it).\n"
                     "#include \"Assets.h\"\n\n" + "\n\n".join(defs) + "\n", newline="\n")
    print(f"assets: {total} bytes of data, {len(sprites)} sprites")


if __name__ == "__main__":
    main()
