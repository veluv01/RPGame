"""CHYacht asset pipeline.

    python tools/assets.py           # fetch-check, build src/assets/*, write previews

Sources:
  * Press Play On Tape's Blackjack art (Apache-2.0), cloned by
    `git clone https://github.com/Press-Play-On-Tape/Blackjack tools/.cache/ppot`
    and pinned to PPOT_COMMIT below: the dealer (who plays the house's hand)
    and his expressions. Monochrome PNGs are recoloured here.
  * CHBlackjack's hand-painted dealer, tools/art/dealer.png (palette-exact,
    the whole 48x42 dealer, replacing the recoloured PPOT bust).
  * New art drawn for this game: tools/art/logo.txt (the "Yacht Dice" title).
  * The chips as palette-letter text (tools/art/chip_*.txt).

Outputs:
  src/assets/Assets.h / Assets.cpp   - generated, do not edit
  build/assets/*.png                 - previews on the real palette
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PPOT = HERE / ".cache" / "ppot"
PPOT_COMMIT = "72a6b1b7c583971568d92eca39c81b8cb99def29"
ART = HERE / "art"
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))     # the repository's tools/: artlib (shared art in tools/art/common)
import artlib  # noqa: E402
DEALER_PNG = artlib.art(HERE, "dealer.png")
OUT_H = ROOT / "src" / "assets" / "Assets.h"
OUT_C = ROOT / "src" / "assets" / "Assets.cpp"
PREVIEW = ROOT / "build" / "assets"

# Must match the RPGame library's palette (pal::HOUSE in
# platform/board/arduino/RPGame/libraries/RPGame/src/rpgame/Palette.cpp).
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]
# Letters used in tools/art/*.txt and recipes. ' ' / '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16


def rgb(i):
    c = PALETTE[i]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------
def ensure_ppot():
    if not PPOT.exists():
        subprocess.run(["git", "clone", "--quiet", "https://github.com/Press-Play-On-Tape/Blackjack.git",
                        str(PPOT)], check=True)
    subprocess.run(["git", "-C", str(PPOT), "checkout", "--quiet", PPOT_COMMIT], check=True)


def load_mono(rel):
    """PPOT PNG -> rows of 1 (white), 0 (black) or None (transparent)."""
    im = Image.open(PPOT / "assets" / rel).convert("RGBA")
    w, h = im.size
    px = im.load()
    return [[(1 if sum(px[x, y][:3]) > 384 else 0) if px[x, y][3] > 0 else None
             for x in range(w)] for y in range(h)]


def load_art(name):
    """tools/art/<name>.txt: palette letters, one row per line; '#' starts a comment line."""
    rows = [ln.rstrip("\n") for ln in (artlib.art(HERE, f"{name}.txt")).read_text().splitlines()
            if ln and not ln.startswith("#")]
    w = max(len(r) for r in rows)
    return [[TRANSPARENT if ch in " ." else LETTER[ch] for ch in r.ljust(w)] for r in rows]


def load_png(path):
    """A hand-painted PNG -> rows of palette indices. Every opaque pixel must
    be exactly one of the palette colours; alpha 0 is transparent."""
    lut = {rgb(i): i for i in range(15)}          # FX_B (15) is GOLD's colour
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


def load_sheet(name, sizes=None):
    """tools/art/<name>.txt with several glyphs per line separated by spaces.
    Returns a list of images; checks each against sizes [(w, h), ...] if given."""
    rows = [ln.split() for ln in (artlib.art(HERE, f"{name}.txt")).read_text().splitlines()
            if ln.strip() and not ln.startswith("#")]
    n = len(rows[0])
    imgs = []
    for i in range(n):
        img = [[TRANSPARENT if ch in "." else LETTER[ch] for ch in r[i]] for r in rows]
        if sizes:
            w, h = sizes[i] if isinstance(sizes, list) else sizes
            assert len(img) == h and all(len(r) == w for r in img), f"{name} glyph {i}: expected {w}x{h}"
        imgs.append(img)
    return imgs


def sprites_array(header, name):
    """Decode an Arduboy Sprites-format array (w, h, then 8-row pages of column bytes)."""
    import re
    text = (PPOT / "Blackjack" / "src" / "images" / header).read_text(encoding="utf-8", errors="replace")
    m = re.search(r"PPOT\[\]\s*=\s*\{(.*?)\};" if name == "PPOT" else name + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"#\w+[^\n]*", "", body)
    nums = [int(t, 0) for t in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]
    w, h = nums[0], nums[1]
    data = nums[2:]
    img = [[0] * w for _ in range(h)]
    for page in range((h + 7) // 8):
        for x in range(w):
            b = data[page * w + x]
            for bit in range(8):
                y = page * 8 + bit
                if y < h and (b >> bit) & 1:
                    img[y][x] = 1
    return img


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------
def pack_cols(bits):
    """1 bpp rows -> column bytes, bit 0 = top (<= 8 rows)."""
    h, w = len(bits), len(bits[0])
    assert h <= 8
    return [sum(1 << y for y in range(h) if bits[y][x]) for x in range(w)]


def pack_rows1(bits):
    """1 bpp rows -> MSB-first row bytes."""
    out = []
    for row in bits:
        for x0 in range(0, len(row), 8):
            b = 0
            for i in range(8):
                if x0 + i < len(row) and row[x0 + i]:
                    b |= 0x80 >> i
            out.append(b)
    return out


def pack_span1(bits):
    """Solid 1 bpp shape -> per row: n, then (x0, len) pairs. Drawn as hlines."""
    out = [len(bits[0]), len(bits)]
    for row in bits:
        runs, x = [], 0
        while x < len(row):
            if row[x]:
                s = x
                while x < len(row) and row[x]:
                    x += 1
                runs.append((s, x - s))
            else:
                x += 1
        out.append(len(runs))
        for s, n in runs:
            out += [s, n]
    return out


def pack_span4(img, trans=TRANSPARENT):
    """Colour image -> w, h, then per row: n, then n bytes of (len-1)<<4 | colour:
    the span4 format of the RPGame library's sprite4 (RPGfx's gfx_sprite4 reads
    it too). Transparent runs use colour 15, which sprite4 skips, so the art
    never draws FX_B."""
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
        # Trailing transparency is implicit.
        while runs and runs[-1][1] == trans:
            runs.pop()
        out.append(len(runs))
        for n, c in runs:
            out.append(((n - 1) << 4) | (15 if c == trans else c))
    return out


# ---------------------------------------------------------------------------
# Previews
# ---------------------------------------------------------------------------
def preview(name, img, scale=6, bg=3):
    h, w = len(img), len(img[0])
    im = Image.new("RGB", (w, h), rgb(bg))
    for y in range(h):
        for x in range(w):
            if img[y][x] != TRANSPARENT:
                im.putpixel((x, y), rgb(img[y][x]))
    PREVIEW.mkdir(parents=True, exist_ok=True)
    im.resize((w * scale, h * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


def mono_preview(name, bits, scale=6):
    preview(name, [[1 if b else TRANSPARENT for b in row] for row in bits], scale, bg=0)


# ---------------------------------------------------------------------------
# The dealer: PPOT's 48x48 bust + 24x24 face, recoloured.
# ---------------------------------------------------------------------------
DEALER_ROWS = 36      # everything below is hidden by the table rail


def dealer_template(face_file="dealer/Dealer_FaceNormal.png"):
    bust = load_mono("dealer/Dealer_BlankFace.png")
    mask = load_mono("dealer/Dealer_Mask.png")
    face = load_mono(face_file)
    t = []
    for y in range(48):
        row = []
        for x in range(48):
            m = mask[y][x] if x < 47 else None
            v = " "
            if m:
                v = "."
            if bust[y][x]:
                v = "#"
            fx, fy = x - 12, y - 10
            if 0 <= fx < 24 and 0 <= fy < 24:
                v = "o" if face[fy][fx] else ("f" if v != " " else v)
            row.append(v)
        t.append(row)
    return t


def colour_dealer(t, variant=0):
    """Region rules turning the mono template into colour. variant 1 = the
    alternate dealer (replaces PPOT's Mario mode, which is Nintendo fan art)."""
    hair = LETTER["b"] if variant == 0 else LETTER["k"]
    vest = LETTER["m"] if variant == 0 else LETTER["n"]
    tie = LETTER["r"] if variant == 0 else LETTER["y"]
    K, P, W = LETTER["k"], LETTER["p"], LETTER["w"]
    img = [[TRANSPARENT] * 48 for _ in range(48)]
    for y in range(48):
        for x in range(48):
            v = t[y][x]
            if v == " ":
                continue
            if y <= 33:                                    # head
                if v == "#":
                    c = K
                    if y == 9 and 12 <= x <= 24:
                        c = LETTER["y"] if variant == 0 else LETTER["s"]   # hair shine
                elif v == "o":
                    c = P
                elif v == "f":                             # dark pixel inside the face
                    c = K
                    if 17 <= y <= 18:
                        c = hair                           # eyebrows
                    if 23 <= y <= 25:
                        c = LETTER["b"]                    # nose shading
                    if y == 27 or y == 28:
                        c = hair                           # moustache
                    if y == 29:
                        c = LETTER["m"]                    # mouth
                    if y >= 31:
                        c = LETTER["b"]                    # jaw shadow
                    if y <= 12:
                        c = hair                           # fringe strands
                else:                                      # '.': black interior
                    if y <= 16:
                        c = hair
                    elif y <= 29:
                        c = P if (x <= 11 or x >= 35) else hair   # ears
                    else:
                        c = P
                    if y == 0 or (x in (7, 8) and y <= 16 and v == "."):
                        c = TRANSPARENT if y == 0 else c
                img[y][x] = c
            else:                                          # shoulders, rows 34+
                if v == "#":
                    c = vest
                else:
                    c = K
                img[y][x] = c
    # Eyes: whites with dark pupils (PPOT draws them as boxes).
    for y in range(20, 23):
        for x in list(range(16, 22)) + list(range(25, 31)):
            if img[y][x] == P:
                img[y][x] = W
    # Shirt, collar and bow tie below the chin.
    for y in range(34, 42):
        for x in range(14, 34):
            if t[y][x] == ".":
                img[y][x] = W
    for (x, y) in [(20, 39), (21, 39), (22, 40), (23, 40), (24, 40), (25, 40), (26, 39), (27, 39),
                   (20, 40), (21, 40), (26, 40), (27, 40), (20, 41), (21, 41), (26, 41), (27, 41),
                   (22, 41), (23, 41), (24, 41), (25, 41)]:
        img[y][x] = tie
    # Mask halo (row 0 and the outer '.' ring) stays transparent.
    for y in range(48):
        for x in range(48):
            if t[y][x] == "." and (y == 0 or x == 0 or t[y][x - 1] == " " or (x < 47 and t[y][x + 1] == " ")):
                img[y][x] = TRANSPARENT
    return img[:DEALER_ROWS + 6]


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
class Out:
    def __init__(self):
        self.h, self.c = [], []

    def array(self, name, data, ctype="uint8_t", comment=""):
        if comment:
            self.c.append(f"// {comment}")
        body = ",".join(f"0x{v:02X}" if ctype == "uint8_t" else str(v) for v in data)
        lines = [body[i:i + 110] for i in range(0, len(body), 110)]
        self.c.append(f"const {ctype} {name}[{len(data)}] = {{\n  " + "\n  ".join(lines) + "\n};")
        self.h.append(f"extern const {ctype} {name}[{len(data)}];")

    def const(self, name, value):
        self.h.append(f"constexpr int {name} = {value};")

    def write(self):
        hdr = ("// GENERATED by tools/assets.py - do not edit.\n"
               "// The dealer derives from Press-Play-On-Tape/Blackjack (Apache-2.0) by\n"
               "// filmote & vampirics, recoloured 2026 for RPGame by bateske (CHBlackjack).\n"
               "// See NOTICE.\n")
        OUT_H.parent.mkdir(parents=True, exist_ok=True)
        OUT_H.write_text(hdr + "#pragma once\n#include <stdint.h>\n\n" + "\n".join(self.h) + "\n")
        OUT_C.write_text(hdr + '#include "Assets.h"\n\n' + "\n".join(self.c) + "\n")


def load_bits(name):
    """tools/art/<name>.txt: '#' = set, anything else clear; ';' starts a comment line."""
    rows = [ln for ln in (artlib.art(HERE, f"{name}.txt")).read_text().splitlines() if ln and not ln.startswith(";")]
    w = max(len(r) for r in rows)
    return [[1 if x < len(r) and r[x] == "#" else 0 for x in range(w)] for r in rows]


def main():
    ensure_ppot()
    o = Out()

    # Dealer: PPOT's bust recoloured by rule, or tools/art/dealer.png when it
    # exists (CHBlackjack's hand-painted one).
    t = dealer_template()
    ruled = colour_dealer(t, 0)
    img = ruled
    if DEALER_PNG.exists():
        img = load_png(DEALER_PNG)
        assert len(img) == len(ruled) and all(len(r) == 48 for r in img),             f"{DEALER_PNG.name}: must be 48x{len(ruled)}"
    o.array("DEALER", pack_span4(img), comment="PPOT dealer, recoloured and retouched, row spans")
    preview("dealer", img, bg=11)

    # Face expressions. The normal face is a 24x18 patch (row spans) at (12,14)
    # of the dealer; every other expression is stored as the pixels that differ
    # from it (16-bit words: index y*24+x << 4 | colour), a fraction of the size.
    # PPOT's expressions are drawn over the rule-built face; what each one
    # changes there is applied over the dealer actually in use.
    def face_patch(f):
        e = colour_dealer(dealer_template(f"dealer/{f}.png"), 0)
        return [row[12:36] for row in e[14:32]]

    ruled_normal = [row[12:36] for row in ruled[14:32]]
    normal = [row[12:36] for row in img[14:32]]

    def rebase(e):
        return [[e[y][x] if e[y][x] != ruled_normal[y][x] else normal[y][x] for x in range(24)]
                for y in range(18)]

    o.array("FACE_NORMAL", pack_span4(normal), comment="face patch 24x18 at (12,14), row spans")
    preview("face_normal", normal, bg=11)
    faces = {}
    for f, nm in [("Dealer_FaceAngry", "ANGRY"), ("Dealer_FaceRaisedEye", "RAISED"),
                  ("Dealer_FaceEyesClosed", "BLINK")]:
        faces[nm] = rebase(face_patch(f))

    # Chips as span sprites, coloured per use by a remap (the shapes are
    # CHBlackjack's chip, captured; Chips.cpp).
    for f in ["chip_top", "chip_side", "chip_small_top", "chip_small_side"]:
        img = load_art(f)
        o.array(f.upper(), pack_span4(img), comment=f"{f} {len(img[0])}x{len(img)}, row spans")
        preview(f, img, bg=3)

    # The "Yacht Dice" title lettering (new for this game).
    logo = load_bits("logo")
    o.array("LOGO", pack_rows1(logo), comment=f"'Yacht Dice' title lettering {len(logo[0])}x{len(logo)}, MSB-first rows")
    o.const("LOGO_W", len(logo[0]))
    o.const("LOGO_H", len(logo))
    mono_preview("logo", logo)

    # Expressions drawn for CHBlackjack, edited from the normal face patch.
    base = normal
    L = LETTER

    def edit(pixels):
        q = [row[:] for row in base]
        for (x, y, c) in pixels:
            q[y][x] = L[c] if isinstance(c, str) else c
        return q

    faces["SMILE"] = edit([(x, 15, "p") for x in range(8, 16)] +
                          [(7, 14, "m"), (16, 14, "m"), (8, 15, "m"), (15, 15, "m")] +
                          [(x, 16, "m") for x in range(9, 15)] + [(x, 15, "w") for x in range(9, 15)])
    faces["SURPRISED"] = edit([(x, 4, "p") for x in range(3, 20)] +
                              [(x, 3, "b") for x in list(range(4, 9)) + list(range(15, 20))] +
                              [(x, 15, "p") for x in range(8, 16)] +
                              [(x, 14, "k") for x in range(10, 14)] + [(x, 17, "k") for x in range(10, 14)] +
                              [(9, 15, "k"), (9, 16, "k"), (14, 15, "k"), (14, 16, "k")] +
                              [(x, y, "m") for x in range(10, 14) for y in (15, 16)])
    faces["TALK"] = edit([(x, 15, "k") for x in range(9, 15)] + [(x, 16, "m") for x in range(10, 14)])
    order = ["ANGRY", "RAISED", "BLINK", "SMILE", "SURPRISED", "TALK"]
    words, starts = [], []
    for nm in order:
        starts.append(len(words))
        img = faces[nm]
        for y in range(18):
            for x in range(24):
                if img[y][x] != normal[y][x]:
                    c = 15 if img[y][x] == TRANSPARENT else img[y][x]
                    words.append(((y * 24 + x) << 4) | c)
        preview(f"face_{nm.lower()}", img, bg=11)
    starts.append(len(words))
    o.array("FACE_EDITS", words, "uint16_t", comment="expression edits: (y*24+x)<<4 | colour; " + " ".join(order))
    o.array("FACE_EDIT_AT", starts, "uint16_t", comment="start of each expression in FACE_EDITS, plus end")

    o.write()
    print(f"wrote {OUT_H.relative_to(ROOT)} and {OUT_C.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
