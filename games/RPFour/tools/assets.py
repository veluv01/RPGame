"""CHFour asset pipeline.

    python tools/assets.py         # build src/assets/*, write previews to build/assets/

Sources, all in tools/art/:
  * dealer.png, faces.png: the dealer and his seven expressions (CHRoulette's
    files, which are CHBlackjack's dealer).
  * disc.png / disc_big.png if present (palette-exact, hand-finished); else
    the disc is drawn here (and written to gen/, a starting point to edit).
  * hand.png, else hand.txt: the pointing glove.
  * sides.txt: the palette swaps that dress the disc as each side's.
  * font.txt: the display font.

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

# Must match pal::HOUSE in the RPGame library
# (platform/board/arduino/RPGame/libraries/RPGame/src/rpgame/Palette.cpp).
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
# Letters used in tools/art/*.txt. ' ' / '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]


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


def draw_disc(size):
    """A disc from above in neutral tones, a casino chip in the series' style:
    WHITE face, SILVER shade round the lower right and an inner ring, a SKIN
    glint top left, an INK outline; the big one (the close-ups) also has the
    BLUE inserts round its rim. The sides recolour it (sides.txt)."""
    import math
    W, P, S, K, E, T = 1, 12, 5, 0, 10, TRANSPARENT
    mid = (size - 1) / 2
    R = size / 2
    img = []
    for y in range(size):
        row = []
        for x in range(size):
            dx, dy = x - mid, y - mid
            r = math.hypot(dx, dy) / R            # 0 centre .. 1 edge
            a = math.degrees(math.atan2(dy, dx)) % 360
            if r > 1.02:
                c = T
            elif r > 0.86:
                c = K
            elif r > 0.58:
                if size >= 16 and (a + 15) % 60 < 30:
                    c = E
                else:
                    c = S if 20 < a < 110 else W
            elif r > 0.44:
                c = S
            else:
                c = P if (dx < -0.08 * size and dy < -0.08 * size and r > 0.2) else W
            row.append(c)
        img.append(row)
    return img


def cell_frame(disc, cell=12):
    """A square of the board, BLUE with the disc's shape cut out of it: drawn
    over a falling disc so it passes behind the board."""
    pad = (cell - len(disc)) // 2
    img = [[10] * cell for _ in range(cell)]
    for y, row in enumerate(disc):
        for x, v in enumerate(row):
            if v != TRANSPARENT:
                img[pad + y][pad + x] = TRANSPARENT
    return img


FACES = ["NORMAL", "ANGRY", "RAISED", "BLINK", "SMILE", "SURPRISED", "TALK"]


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

    # The dealer (CHBlackjack's, as CHRoulette carries him: dealer.png and
    # faces.png are that game's files, unchanged).
    dealer = load_png(artlib.art(HERE, "dealer.png"))
    assert len(dealer) == 42 and all(len(r) == 48 for r in dealer), "dealer.png must be 48x42"
    data = pack_span4(dealer)
    defs.append(c_array("DEALER", data))
    decls.append(f"extern const uint8_t DEALER[{len(data)}];                          // the dealer, span4, 48 x 42")
    total += len(data)
    preview("dealer", [dealer], scale=4, bg=11)
    # Faces: NORMAL as a 24x18 patch at (12,14); every other expression as the
    # pixels that differ from it (16-bit words: index y*24+x << 4 | colour).
    sheet = load_png(artlib.art(HERE, "faces.png"))
    assert len(sheet) == 18 and len(sheet[0]) == 24 * len(FACES), "faces.png must be 7 cells of 24x18"
    faces = [[row[24 * k:24 * k + 24] for row in sheet] for k in range(len(FACES))]
    normal = faces[0]
    data = pack_span4(normal)
    defs.append(c_array("FACE_NORMAL", data))
    decls.append(f"extern const uint8_t FACE_NORMAL[{len(data)}];                     // his face, a 24 x 18 patch at (12, 14)")
    total += len(data)
    words, starts = [], []
    for k in range(1, len(FACES)):
        starts.append(len(words))
        for y in range(18):
            for x in range(24):
                if faces[k][y][x] != normal[y][x]:
                    c = 15 if faces[k][y][x] == TRANSPARENT else faces[k][y][x]
                    words.append(((y * 24 + x) << 4) | c)
    starts.append(len(words))
    defs.append(f"const uint16_t FACE_EDITS[{len(words)}] = {{\n" +
                "\n".join("    " + ", ".join(str(v) for v in words[i:i + 16]) + "," for i in range(0, len(words), 16)) + "\n};")
    defs.append(f"const uint16_t FACE_EDIT_AT[{len(starts)}] = {{" + ", ".join(str(v) for v in starts) + "};")
    decls.append(f"extern const uint16_t FACE_EDITS[{len(words)}];                    // the other expressions, as the pixels that differ:\n"
                 f"extern const uint16_t FACE_EDIT_AT[{len(starts)}];                     // (y * 24 + x) << 4 | colour; each one's start, and the end")
    total += 2 * len(words) + 2 * len(starts)
    preview("faces", faces, scale=4, bg=11)

    # The disc, and at twice the size for the close-ups.
    disc = load_png(artlib.art(HERE, "disc.png")) if (artlib.art(HERE, "disc.png")).exists() else draw_disc(10)
    save_png(GEN / "disc.png", disc)
    data = pack_span4(disc)
    defs.append(c_array("DISC", data))
    defs.append(remap_table("DISC_REMAP", sides["disc"]))
    decls.append(f"extern const uint8_t DISC[{len(data)}];                            // span4, {len(disc[0])} x {len(disc)}, neutral tones\n"
                 "extern const uint8_t DISC_REMAP[2][16];                     // ... as RED's, as GOLD's (tools/art/sides.txt)")
    total += len(data) + 32
    preview("disc", [remapped(disc, m) for m in sides["disc"]], bg=10)
    big = load_png(artlib.art(HERE, "disc_big.png")) if (artlib.art(HERE, "disc_big.png")).exists() else draw_disc(20)
    save_png(GEN / "disc_big.png", big)
    data = pack_span4(big)
    defs.append(c_array("DISC_BIG", data))
    decls.append(f"extern const uint8_t DISC_BIG[{len(data)}];                       // the same disc, {len(big[0])} x {len(big)}: the close-ups")
    total += len(data)
    preview("disc_big", [remapped(big, m) for m in sides["disc"]], scale=6, bg=10)
    frame = cell_frame(disc)
    data = pack_span4(frame)
    defs.append(c_array("CELL_FRAME", data))
    decls.append(f"extern const uint8_t CELL_FRAME[{len(data)}];                      // a square of the board with its hole: over a falling disc")
    total += len(data)

    # The pointing hand (span4).
    hand = source("hand")
    data = pack_span4(hand)
    defs.append(c_array("HAND", data))
    tip = [x for x, v in enumerate(hand[-1]) if v != TRANSPARENT]
    decls.append(f"extern const uint8_t HAND[{len(data)}];                            // span4, fingertip on the bottom row\n"
                 f"constexpr uint8_t HAND_TIP = {(tip[0] + tip[-1]) // 2};                           // its column")
    total += len(data)
    preview("hand", [hand])

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
    OUT_H.write_text("// Generated by tools/assets.py - do not edit.\n"
                     "// The dealer is Press Play On Tape's (Apache-2.0), as recoloured for\n"
                     "// CHBlackjack; the glove is CHChess's. See NOTICE.\n"
                     "#pragma once\n#include <stdint.h>\n\n"
                     + "\n".join(decls) + "\n", newline="\n")
    OUT_C.write_text("// Generated by tools/assets.py - do not edit.\n"
                     "// The art is in tools/art/ (see tools/assets.py for how to redraw it).\n"
                     "#include \"Assets.h\"\n\n" + "\n\n".join(defs) + "\n", newline="\n")
    print(f"assets: {total} bytes of data")

    # The dealer must be the one the other tables have.
    import re
    ref = ROOT.parent / "CHBlackjack" / "src" / "assets" / "Assets.cpp"
    if ref.exists():
        text = ref.read_text(encoding="utf-8")
        m = re.search(r"\bDEALER\[\d*\]\s*=\s*\{(.*?)\};", text, re.S)
        theirs = [int(t, 0) for t in re.findall(r"0x[0-9A-Fa-f]+|\d+", re.sub(r"//[^\n]*", "", m.group(1)))] if m else None
        print("  the dealer: " + ("identical to CHBlackjack's" if theirs == pack_span4(dealer) else "DIFFERS from CHBlackjack's"))


if __name__ == "__main__":
    main()
