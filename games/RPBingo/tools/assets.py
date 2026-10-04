"""CHBingo asset pipeline.

    python tools/assets.py           # build src/assets/Assets.*, write previews

Sources, all in tools/art/ (palette-exact PNGs, or text with one palette
letter per pixel, '.' transparent, '#' set for 1 bpp lettering, '# '
starting a comment line):
  * dealer.png, faces.png - CHBlackjack's dealer (Press Play On Tape's,
    recoloured and retouched), here the caller, and his seven expressions
    as 24x18 face patches side by side: NORMAL ANGRY RAISED BLINK SMILE
    SURPRISED TALK.
  * hand.png - CHChess's pointing glove (also turned to point right).
  * logo.txt - the "Bingo" title lettering, in CHBlackjack's and CHRoulette's
    letter style.
  * broke1/2.txt - PPOT's lettering for the broke screen.

The dealer, faces and lettering come out byte-identical to the arrays in
CHBlackjack: if that repo sits next to this one, the build checks it.

Outputs:
  src/assets/Assets.h / Assets.cpp   - generated, do not edit
  build/assets/*.png                 - previews on the real palette
"""
import re
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

# Must match the RPGame library's pal::HOUSE (platform/board/arduino/RPGame/libraries/RPGame/src/rpgame/Palette.cpp).
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
# Letters used in tools/art/*.txt. '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16
FACES = ["NORMAL", "ANGRY", "RAISED", "BLINK", "SMILE", "SURPRISED", "TALK"]


def rgb(i):
    c = PALETTE[i]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------
def text_rows(name):
    """Pixel rows of a text art file: comment lines start with '# ' (a pixel
    row never holds a space)."""
    return [ln.rstrip() for ln in (artlib.art(HERE, name)).read_text(encoding="utf-8").splitlines()
            if ln.strip() and not ln.startswith("# ")]


def load_art(name):
    rows = text_rows(name)
    w = max(len(r) for r in rows)
    return [[TRANSPARENT if ch == "." else LETTER[ch] for ch in r.ljust(w, ".")] for r in rows]


def load_bits(name):
    rows = text_rows(name)
    w = max(len(r) for r in rows)
    return [[1 if ch == "#" else 0 for ch in r.ljust(w, ".")] for r in rows]


def load_png(path):
    """A palette-exact PNG -> rows of palette indices; alpha 0 is transparent."""
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


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------
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


def pack_span4(img, trans=TRANSPARENT):
    """Colour image -> w, h, then per row: n, then n bytes of (len-1)<<4 | colour:
    the game's sprite4 format. Transparent runs use colour 15, which sprite4
    skips, so the art never draws FX_B."""
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
        while runs and runs[-1][1] == trans:        # trailing transparency is implicit
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
# Output
# ---------------------------------------------------------------------------
class Out:
    def __init__(self):
        self.h, self.c = [], []
        self.arrays = {}

    def array(self, name, data, ctype="uint8_t", comment=""):
        self.arrays[name] = list(data)
        self.h.append(f"extern const {ctype} {name}[{len(data)}];" + (f"  // {comment}" if comment else ""))
        fmt = "0x{:02X}" if ctype == "uint8_t" else "0x{:04X}"
        body = ",".join(fmt.format(v) for v in data)
        width = 100 if ctype == "uint8_t" else 98
        chunks, line = [], ""
        for tok in body.split(","):
            if len(line) + len(tok) + 1 > width:
                chunks.append(line)
                line = ""
            line += tok + ","
        chunks.append(line)
        if comment:
            self.c.append(f"// {comment}")
        self.c.append(f"const {ctype} {name}[{len(data)}] = {{\n  " + "\n  ".join(chunks) + "\n};\n")

    def const(self, name, value, comment=""):
        self.h.append(f"constexpr int {name} = {value};" + (f"  // {comment}" if comment else ""))

    def write(self):
        head = ("// GENERATED by tools/assets.py - do not edit.\n"
                "// The caller and the broke-screen lettering are Press Play On Tape's\n"
                "// (Apache-2.0, by filmote & vampirics), as recoloured for CHBlackjack; the\n"
                "// glove is CHChess's; the rest was drawn for this game. See NOTICE.\n")
        OUT_H.parent.mkdir(parents=True, exist_ok=True)
        OUT_H.write_text(head + "#pragma once\n#include <stdint.h>\n\n" + "\n".join(self.h) + "\n",
                         encoding="utf-8", newline="\n")
        OUT_C.write_text(head + '#include "Assets.h"\n\n' + "\n".join(self.c), encoding="utf-8", newline="\n")


def sibling_array(game, name):
    """An array from a sibling game's generated Assets.cpp, or None."""
    p = ROOT.parent / game / "src" / "assets" / "Assets.cpp"
    if not p.exists():
        return None
    text = p.read_text(encoding="utf-8")
    m = re.search(r"\b" + name + r"\[\d*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        return None
    return [int(t, 0) for t in re.findall(r"0x[0-9A-Fa-f]+|\d+", re.sub(r"//[^\n]*", "", m.group(1)))]


# ---------------------------------------------------------------------------
def main():
    o = Out()

    # The caller.
    dealer = load_png(artlib.art(HERE, "dealer.png"))
    assert len(dealer) == 42 and all(len(r) == 48 for r in dealer), "dealer.png must be 48x42"
    o.array("DEALER", pack_span4(dealer), comment="the caller 48x42, row spans")
    preview("dealer", dealer, bg=11)
    alt = [0, 1, 2, 3, 4, 5, 8, 11, 8, 0, 10, 11, 12, 13, 14, 15]
    o.array("DEALER_ALT_REMAP", alt, comment="the NIGHT caller: remap applied to DEALER")
    preview("dealer_alt", [[v if v == TRANSPARENT else alt[v] for v in row] for row in dealer], bg=11)

    # Faces: NORMAL as a 24x18 patch at (12,14); every other expression as the
    # pixels that differ from it (16-bit words: index y*24+x << 4 | colour).
    sheet = load_png(artlib.art(HERE, "faces.png"))
    assert len(sheet) == 18 and len(sheet[0]) == 24 * len(FACES), "faces.png must be 7 cells of 24x18"
    faces = [[row[24 * k:24 * k + 24] for row in sheet] for k in range(len(FACES))]
    normal = faces[0]
    o.array("FACE_NORMAL", pack_span4(normal), comment="face patch 24x18 at (12,14), row spans")
    words, starts = [], []
    for k in range(1, len(FACES)):
        starts.append(len(words))
        for y in range(18):
            for x in range(24):
                if faces[k][y][x] != normal[y][x]:
                    c = 15 if faces[k][y][x] == TRANSPARENT else faces[k][y][x]
                    words.append(((y * 24 + x) << 4) | c)
        preview(f"face_{FACES[k].lower()}", faces[k], bg=11)
    starts.append(len(words))
    o.array("FACE_EDITS", words, "uint16_t", comment="expression edits: (y*24+x)<<4 | colour; " + " ".join(FACES[1:]))
    o.array("FACE_EDIT_AT", starts, "uint16_t", comment="start of each expression in FACE_EDITS, plus end")

    # The glove (CHChess): pointing down, fingertip on the bottom row; and
    # turned a quarter and flipped (pointing right, fingertip in the last
    # column) for menus.
    hand = load_png(artlib.art(HERE, "hand.png"))
    o.array("HAND", pack_span4(hand), comment="the glove 13x16, row spans; fingertip on the bottom row")
    tip = [x for x, v in enumerate(hand[-1]) if v != TRANSPARENT]
    o.const("HAND_TIP", (tip[0] + tip[-1]) // 2, "the fingertip's column")
    preview("hand", hand, bg=3)
    w, h = len(hand[0]), len(hand)
    right = [[hand[x][w - 1 - y] for x in range(h)] for y in range(w)]   # 90 degrees anticlockwise
    right = right[::-1]                                  # and flipped: the thumb on top
    o.array("HAND_R", pack_span4(right), comment=f"the glove pointing right {h}x{w}, row spans")
    tip = [y for y, row in enumerate(right) if row[-1] != TRANSPARENT]
    o.const("HAND_R_TIP", (tip[0] + tip[-1]) // 2, "the fingertip's row")
    preview("hand_r", right, bg=3)

    # Lettering.
    logo = load_bits("logo.txt")
    o.array("LOGO", pack_rows1(logo), comment=f"'Bingo' {len(logo[0])}x{len(logo)}, MSB-first rows")
    o.const("LOGO_W", len(logo[0]))
    o.const("LOGO_H", len(logo))
    mono_preview("logo", logo)
    for nm in ("BROKE1", "BROKE2"):
        bits = load_bits(nm.lower() + ".txt")
        o.array(nm, pack_rows1(bits), comment=f"PPOT lettering {len(bits[0])}x{len(bits)}, MSB-first rows")
        o.const(nm + "_W", len(bits[0]))
        mono_preview(nm.lower(), bits)

    o.write()
    print(f"wrote {OUT_H.relative_to(ROOT)} and {OUT_C.relative_to(ROOT)}")

    # The shared art must match the games it came from.
    for game, names in (("CHBlackjack", ["DEALER", "DEALER_ALT_REMAP", "FACE_NORMAL", "FACE_EDITS",
                                         "FACE_EDIT_AT", "BROKE1", "BROKE2"]),
                        ("CHChess", ["HAND"])):
        for nm in names:
            ref = sibling_array(game, nm)
            if ref is None:
                print(f"  ({game} not found next to this repo: {nm} unchecked)")
                break
            if ref != o.arrays[nm]:
                raise SystemExit(f"{nm} differs from {game}'s")
        else:
            print(f"  {', '.join(names)}: identical to {game}'s")


if __name__ == "__main__":
    main()
