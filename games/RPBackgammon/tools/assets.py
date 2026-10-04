"""CHBackgammon asset pipeline.

    python tools/assets.py         # build src/assets/*, write previews to build/assets/

Sources, all in tools/art/:
  * checker.png if present (palette-exact, hand-finished), else the palette
    letters in checker.txt: the one checker, in neutral tones.
  * dice.png if present: the six faces side by side, 12 x 12 each; else they
    are drawn here (and written to gen/dice.png, a starting point to edit).
  * hand.png, else hand.txt: the pointing glove.
  * sides.txt: the palette swaps that dress the checker and dice as each side.

To redraw something in an image editor: copy tools/art/gen/<name>.png to
tools/art/<name>.png, edit it with the 16 palette colours only (see the
swatch in build/assets/palette.png), and run this again.

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

# Must match the RPGame library's pal::HOUSE (platform/board/arduino/RPGame/libraries/RPGame/src/rpgame/Palette.cpp).
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
# Letters used in tools/art/*.txt. ' ' / '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]
DIE = 12


def load_sides():
    """tools/art/sides.txt -> {section: [white, red]}, each art colour -> side colour (16 entries)."""
    out, cur = {}, None
    for ln in (artlib.art(HERE, "sides.txt")).read_text().splitlines():
        words = ln.split("#", 1)[0].split()
        if not words:
            continue
        if words[0].startswith("["):
            cur = out.setdefault(words[0].strip("[]"), [list(range(16)), list(range(16))])
            continue
        a, w, r = (NAMES.index(n) for n in words)
        cur[0][a], cur[1][a] = w, r
    return out


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


def draw_die(face):
    """A die face, 12 x 12, neutral tones: WHITE body, SILVER shade along the
    bottom and right, NAVY pips (2 x 2), an INK outline with the corners cut."""
    W, S, N, K, T = 1, 5, 11, 0, TRANSPARENT
    img = [[W] * DIE for _ in range(DIE)]
    for i in range(DIE):
        img[0][i] = img[DIE - 1][i] = img[i][0] = img[i][DIE - 1] = K
    for i in range(1, DIE - 1):
        img[DIE - 2][i] = S
        img[i][DIE - 2] = S
    for y, x in ((0, 0), (0, DIE - 1), (DIE - 1, 0), (DIE - 1, DIE - 1)):
        img[y][x] = T
    for y, x in ((1, 1), (1, DIE - 2), (DIE - 2, 1), (DIE - 2, DIE - 2)):
        img[y][x] = K
    spots = {1: [4], 2: [2, 6], 3: [2, 4, 6], 4: [0, 2, 6, 8], 5: [0, 2, 4, 6, 8], 6: [0, 2, 3, 5, 6, 8]}[face]
    for s in spots:
        for dy in range(2):
            for dx in range(2):
                img[2 + 3 * (s // 3) + dy][2 + 3 * (s % 3) + dx] = N
    return img


def draw_big_checker():
    """The checker at 16 x 16, for the close-ups: a casino chip from above, in
    the same neutral tones plus BLUE for the inserts round its edge (which the
    sides recolour, as CHBlackjack's chips have them)."""
    import math
    W, P, S, K, E, T = 1, 12, 5, 0, 10, TRANSPARENT
    img = []
    for y in range(16):
        row = []
        for x in range(16):
            dx, dy = x - 7.5, y - 7.5
            r = math.hypot(dx, dy)
            a = math.degrees(math.atan2(dy, dx)) % 360
            if r > 8.1:
                c = T
            elif r > 6.9:
                c = K
            elif r > 4.6:
                # The rim: six inserts, and shade round the lower right.
                c = E if (a + 15) % 60 < 30 else (S if 20 < a < 110 else W)
            elif r > 3.7:
                c = S
            else:
                c = P if (dx < -0.6 and dy < -0.6 and r > 1.6) else W
            row.append(c)
        img.append(row)
    return img


def load_dice():
    png = artlib.art(HERE, "dice.png")
    if png.exists():
        sheet = load_png(png)
        return [[row[f * DIE:(f + 1) * DIE] for row in sheet] for f in range(6)]
    faces = [draw_die(f) for f in range(1, 7)]
    save_png(GEN / "dice.png", [sum((faces[f][y] for f in range(6)), []) for y in range(DIE)])
    return faces


def load_font():
    """tools/art/font.txt -> {char: {"w", "top", "rows"}} (rows of 0/1, all-clear rows trimmed)."""
    font, cur = {}, None
    for ln in (artlib.art(HERE, "font.txt")).read_text().splitlines():
        if not ln.strip() or ln.startswith("# "):           # (glyph rows are only '#' and '.')
            continue
        if ln.startswith("= "):
            parts = ln[2:].split()
            cur = {"top": int(parts[1]) if len(parts) > 1 else 0, "rows": []}
            font[parts[0]] = cur
            continue
        cur["rows"].append([1 if ch == "#" else 0 for ch in ln.rstrip()])
    for c, g in font.items():
        w = max(len(r) for r in g["rows"])
        g["rows"] = [r + [0] * (w - len(r)) for r in g["rows"]]
        while g["rows"] and not any(g["rows"][0]):
            g["rows"].pop(0)
            g["top"] += 1
        while g["rows"] and not any(g["rows"][-1]):
            g["rows"].pop()
        g["w"] = w
    return font


def pack_font(font):
    """The glyphs one after another, each a three-byte header - its
    character, width, top row << 4 | rows - then its rows' bits packed
    tightly (MSB first); a zero byte ends them. (The game finds a glyph by
    walking the list: no index to carry.)"""
    data = []
    for c in sorted(font):
        g = font[c]
        data += [ord(c), g["w"], (g["top"] << 4) | len(g["rows"])]
        bits = [v for row in g["rows"] for v in row]
        for i in range(0, len(bits), 8):
            chunk = bits[i:i + 8] + [0] * (8 - len(bits[i:i + 8]))
            data.append(sum(b << (7 - k) for k, b in enumerate(chunk)))
    return data + [0]


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


def remap_table(name, maps):
    return (f"const uint8_t {name}[2][16] = {{\n" +
            "".join("    {" + ", ".join(str(v) for v in m) + "},\n" for m in maps) + "};")


def main():
    PREVIEW.mkdir(parents=True, exist_ok=True)
    decls, defs = [], []
    total = 0
    sides = load_sides()

    # The checker.
    chk = source("checker")
    save_png(GEN / "checker.png", chk)
    data = pack_span4(chk)
    defs.append(c_array("CHECKER", data))
    defs.append(remap_table("CHECKER_REMAP", sides["checker"]))
    decls.append(f"extern const uint8_t CHECKER[{len(data)}];                         // span4, {len(chk[0])} x {len(chk)}, neutral tones\n"
                 "extern const uint8_t CHECKER_REMAP[2][16];                  // ... as White's, as Red's (tools/art/sides.txt)")
    total += len(data) + 32
    preview("checker", [remapped(chk, m) for m in sides["checker"]])

    # ... and at twice the size, for the close-ups.
    big = load_png(artlib.art(HERE, "checker_big.png")) if (artlib.art(HERE, "checker_big.png")).exists() else draw_big_checker()
    save_png(GEN / "checker_big.png", big)
    data = pack_span4(big)
    defs.append(c_array("CHECKER_BIG", data))
    decls.append(f"extern const uint8_t CHECKER_BIG[{len(data)}];                    // the same checker, {len(big[0])} x {len(big)}: the close-ups")
    total += len(data)
    preview("checker_big", [remapped(big, m) for m in sides["checker"]], scale=6)

    # The dice.
    faces = load_dice()
    blob, offs = [], []
    for f in faces:
        offs.append(len(blob))
        blob += pack_span4(f)
    defs.append(c_array("DICE", blob))
    defs.append("const uint16_t DIE_AT[6] = {" + ", ".join(str(o) for o in offs) + "};")
    defs.append(remap_table("DIE_REMAP", sides["die"]))
    defs.append(remap_table("DIE_USED", sides["used"]))
    decls.append(f"extern const uint8_t DICE[{len(blob)}];                          // span4 faces 1..6, {DIE} x {DIE}: DICE + DIE_AT[face - 1]\n"
                 "extern const uint16_t DIE_AT[6];\n"
                 "extern const uint8_t DIE_REMAP[2][16], DIE_USED[2][16];     // each side's dice; and once played")
    total += len(blob) + 12 + 64
    preview("dice", [remapped(f, sides["die"][0]) for f in faces] + [remapped(f, sides["die"][1]) for f in faces] +
            [remapped(faces[5], sides["used"][0]), remapped(faces[5], sides["used"][1])], scale=6)

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

    # The display font.
    font = load_font()
    data = pack_font(font)
    defs.append(c_array("FONT", data))
    decls.append(f"extern const uint8_t FONT[{len(data)}];                         // the display font (tools/art/font.txt): per glyph its\n"
                 "                                                            // character, width, top << 4 | rows, the rows' bits; 0 ends")
    total += len(data)

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
