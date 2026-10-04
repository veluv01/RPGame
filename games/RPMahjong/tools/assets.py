"""Generate src/assets/Assets.{h,cpp} from the art in tools/art/.

    python tools/assets.py

  * The tile faces, palette-letter text in the game's face order (dots,
    bamboo and characters 1-9, the winds, the dragons, four flowers, four
    seasons), then the back of a tile; a face may use two colours besides the
    tile's own. tools/art/classic.txt (7 x 11) and classic2x.txt (15 x 23,
    for the close-up) are the traditional faces, written by tools/faces.py;
    tools/art/tiles.txt (7 x 11) is the EASY set, with numbers.
  * The pointing hand: tools/art/hand.png (palette-exact, alpha 0 =
    transparent), else palette-letter text in tools/art/hand.txt.

A face becomes an 8 x 12 cell at 2 bits a pixel: the tile's face, its emboss
(the art's shadow: face pixels a pixel down and right of the art, worked
out here), and the two inks; the top row and left column are the tile's
edge, drawn by the game. The stage draws a cell through colours of its
choice, so one cell serves a free tile (embossed), a blocked one (flat), a
flash and a shimmer.

Also writes previews to build/assets/.
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
EMBOSS = 17                     # in a face: the art's shadow
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]
FACES = 42                      # then the back
FACE_W, FACE_H = 7, 11
CELL_W, CELL_H = 8, 12


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


def load_hand():
    png = artlib.art(HERE, "hand.png")
    return load_png(png) if png.exists() else load_art("hand")[0]


# The face sets: (art file, face width, face height, C name, embossed). The
# small traditional faces are left flat: at 7 px an emboss muddies them.
SETS = [("tiles.txt", 7, 11, "EASY", True), ("classic.txt", 7, 11, "CLASSIC", False),
        ("classic2x.txt", 15, 23, "BIG", True)]


def load_tiles(name="tiles.txt", fw=FACE_W, fh=FACE_H):
    """tools/art/<name> -> a list of faces, each fh rows of fw palette indices."""
    faces, block = [], []

    def flush():
        if not block:
            return
        if len(block) != fh:
            raise SystemExit(f"{name}: a row of faces has {len(block)} lines, not {fh}")
        cols = [ln.split() for ln in block]
        n = len(cols[0])
        for k in range(n):
            face = []
            for r in range(fh):
                if len(cols[r]) != n or len(cols[r][k]) != fw:
                    raise SystemExit(f"{name}: face {len(faces)} row {r} is not {fw} wide")
                face.append([TRANSPARENT if ch == "." else LETTER[ch] for ch in cols[r][k]])
            faces.append(face)
        block.clear()

    for ln in (artlib.art(HERE, name)).read_text().splitlines():
        if ln.startswith("#"):
            continue
        if not ln.strip():
            flush()
            continue
        block.append(ln)
    flush()
    if len(faces) != FACES + 1:
        raise SystemExit(f"{name}: {len(faces)} faces, expected {FACES} and the back")
    return faces


def emboss(face):
    """The face with its emboss: EMBOSS where the art's shadow falls (down and right
    of an ink pixel, on bare face)."""
    out = [row[:] for row in face]
    for y in range(1, len(face)):
        for x in range(1, len(face[0])):
            if face[y][x] == TRANSPARENT and face[y - 1][x - 1] not in (TRANSPARENT, EMBOSS):
                out[y][x] = EMBOSS
    return out


def pack_cell(face, k, name="tiles.txt"):
    """A face (fw x fh) -> ((fw+1)*(fh+1)/4 bytes, ink byte). Pixel values: 0 face,
    1 emboss, 2 and 3 the inks (row 0 and column 0, the edge, are 0); two bits a
    pixel, the left pixel in the low bits."""
    inks = sorted({c for row in face for c in row if c not in (TRANSPARENT, EMBOSS)})
    if len(inks) > 2:
        raise SystemExit(f"{name}: face {k} uses {len(inks)} colours ({', '.join(NAMES[c] for c in inks)}); two at most")
    for c in inks:
        if c in (2, 3, 14, 15):
            raise SystemExit(f"{name}: face {k} uses {NAMES[c]}, which changes with the table or the animation")
    inks += [inks[0] if inks else 0] * (2 - len(inks))
    cw, ch = len(face[0]) + 1, len(face) + 1
    out = []
    for y in range(ch):
        px = []
        for x in range(cw):
            c = TRANSPARENT if x == 0 or y == 0 else face[y - 1][x - 1]
            px.append(0 if c == TRANSPARENT else 1 if c == EMBOSS else 2 + inks.index(c))
        for b in range(0, cw, 4):
            out.append(px[b] | px[b + 1] << 2 | px[b + 2] << 4 | px[b + 3] << 6)
    return out, inks[0] | inks[1] << 4


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


def load_logo():
    """tools/art/logo.txt -> rows of 0/1 ('#' set). Comment lines are '# ' and text
    (a row of the picture never has a space in it)."""
    rows = [ln.rstrip() for ln in (artlib.art(HERE, "logo.txt")).read_text().splitlines() if ln.strip() and " " not in ln.strip()]
    w = max(len(r) for r in rows)
    return [[1 if ch == "#" else 0 for ch in r.ljust(w, ".")] for r in rows]


def load_bird():
    """tools/art/bird.txt -> [(name, w, h, [frames])], frames as rows of palette indices."""
    anims, cur, img = [], None, []

    def flush():
        if img:
            cur[3].append([[TRANSPARENT if ch == "." else LETTER[ch] for ch in r] for r in img])
            img.clear()

    for ln in (artlib.art(HERE, "bird.txt")).read_text().splitlines():
        if ln.startswith("@"):
            flush()
            _, name, w, h = ln.split()
            cur = (name, int(w), int(h), [])
            anims.append(cur)
        elif ln.startswith("#"):
            continue
        elif not ln.strip():
            flush()
        else:
            img.append(ln.rstrip())
    flush()
    return anims


def pack_rows1(bits):
    """1 bit a pixel, MSB-first rows, each padded to a whole byte."""
    out = []
    for row in bits:
        for x0 in range(0, len(row), 8):
            b = 0
            for i, v in enumerate(row[x0:x0 + 8]):
                if v:
                    b |= 0x80 >> i
            out.append(b)
    return out


def preview(name, img, scale=6, bg=3):
    h, w = len(img), len(img[0])
    im = Image.new("RGB", (w, h), rgb(bg))
    for y in range(h):
        for x in range(w):
            if img[y][x] != TRANSPARENT:
                im.putpixel((x, y), rgb(img[y][x]))
    im.resize((w * scale, h * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


def draw_tile(img, x0, y0, face, face_col, shade_col, edge=9, side=12, back=9, t=1):
    """A tile as Tile.cpp draws it (body bands t px, edge, embossed face)."""
    cw, ch = len(face[0]) + 1, len(face) + 1
    for d, c in ((2 * t, back), (t, side)):
        for y in range(ch):
            for x in range(cw):
                img[y0 + y + d][x0 + x + d] = c
    for y in range(ch):
        for x in range(cw):
            if x == 0 or y == 0:
                c = edge
            else:
                c = face[y - 1][x - 1]
                c = face_col if c == TRANSPARENT else shade_col if c == EMBOSS else c
            img[y0 + y][x0 + x] = c


def tile_sheet(name, faces, scale, emb=True):
    """Every face as the game draws it (above), and flat (below)."""
    per = 9
    cw, ch = len(faces[0][0]) + 1, len(faces[0]) + 1
    t = 2 if cw > 8 else 1
    px, py = cw + 2 * t + 2, ch + 2 * t + 2
    rows = (len(faces) + per - 1) // per
    img = [[3] * (per * px + 2) for _ in range(rows * 2 * py + 2)]
    for k, face in enumerate(faces):
        x0, y0 = 1 + (k % per) * px, 1 + (k // per) * 2 * py
        e = emboss(face) if emb and k < FACES else face
        draw_tile(img, x0, y0, e, 1, 12, t=t)
        draw_tile(img, x0, y0 + py, e, 1, 1, t=t)
    preview(name, img, scale)


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

    # Tile faces: each set, as cells and their inks.
    for art, fw, fh, cname, emb in SETS:
        faces = load_tiles(art, fw, fh)
        cells, inks = [], []
        for k, face in enumerate(faces):
            cell, ink = pack_cell(emboss(face) if emb and k < FACES else face, k, art)
            cells.append(cell)
            inks.append(ink)
        n = len(cells[0])
        lines = [f"const uint8_t TILE_CELL_{cname}[{len(faces)}][{n}] = {{"]
        for cell in cells:
            lines.append("    {" + ", ".join(str(v) for v in cell) + "},")
        lines.append("};")
        lean = cname == "EASY"            # left out of device debug builds (config.h CHMJ_LEAN)
        defs.append(("#if !CHMJ_LEAN\n" if lean else "") + "\n".join(lines))
        defs.append(c_array(f"TILE_INK_{cname}", inks) + ("\n#endif" if lean else ""))
        decls.append(f"extern const uint8_t TILE_CELL_{cname}[{len(faces)}][{n}];   // {fw + 1}x{fh + 1}, 2 bpp: "
                     f"0 face, 1 emboss, 2 and 3 the inks (tools/art/{art})\n"
                     f"extern const uint8_t TILE_INK_{cname}[{len(faces)}];          // a face's inks: low nibble, high nibble")
        total += len(cells) * n + len(inks)
        tile_sheet(f"tiles_{cname.lower()}", faces, 8 if fw < 10 else 5, emb)
    decls.insert(0, f"constexpr uint8_t TILE_BACK = {FACES};                         // the back of a tile, after the faces")

    # The title's lettering (1 bpp).
    logo = load_logo()
    data = pack_rows1(logo)
    defs.append(c_array("LOGO", data))
    decls.append("extern const uint8_t LOGO[];                                 // the title, 1 bpp MSB-first rows (tools/art/logo.txt)\n"
                 f"constexpr uint8_t LOGO_W = {len(logo[0])}, LOGO_H = {len(logo)};")
    total += len(data)
    preview("logo", [[1 if v else TRANSPARENT for v in r] for r in logo], 4)

    # The sparrow (span4), its animations from tools/art/bird.txt.
    anims = load_bird()
    names = []
    for name, w, h, frames in anims:
        for k, img in enumerate(frames):
            data = pack_span4(img)
            defs.append(c_array(f"BIRD_{name.upper()}_{k}", data))
            total += len(data)
        defs.append(f"static const uint8_t *const BIRD_{name.upper()}[{len(frames)}] = {{"
                    + ", ".join(f"BIRD_{name.upper()}_{k}" for k in range(len(frames))) + "};")
        names.append((name, len(frames), w, h))
        preview(f"bird_{name}", [x for f in frames for x in f + [[TRANSPARENT] * w]], 6)
    defs.append(f"const BirdAnim BIRD[{len(names)}] = {{\n"
                + "".join(f"    {{BIRD_{n.upper()}, {k}, {w}, {h}}},\n" for n, k, w, h in names) + "};")
    decls.append("// The sparrow's animations (tools/art/bird.txt), facing left: span4 frames.\n"
                 "struct BirdAnim { const uint8_t *const *frames; uint8_t n, w, h; };\n"
                 f"extern const BirdAnim BIRD[{len(names)}];\n"
                 "enum BirdAnimId : uint8_t { " + ", ".join(f"B_{n.upper()}" for n, *_ in names) + " };")

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
                     "// Tile faces and the pointing hand, drawn in tools/art/.\n"
                     "#include \"../../config.h\"\n#include \"Assets.h\"\n\n" + "\n\n".join(defs) + "\n", newline="\n")
    print(f"assets: {total} bytes of data")


if __name__ == "__main__":
    main()
