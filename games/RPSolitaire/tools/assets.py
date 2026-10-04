"""CHSolitaire asset pipeline.

    python tools/assets.py           # build src/assets/*, write previews

Sources, all palette-letter text or palette-exact PNGs in tools/art/:
  * suits.txt   - Press Play On Tape's 5x6 suit glyphs (Apache-2.0, via
                  CHBlackjack): clubs, diamonds, hearts, spades.
  * ranks.txt, pip9.txt, pip13.txt, court.txt - CHBlackjack's card art
                  (ranks A..K, pips hearts/diamonds/spades/clubs; the suits
                  are reordered here into the game's: c d s h).
  * hand.png (or hand.txt) - CHChess's pointing glove.
  * logo.txt    - the title lettering (first drawn by tools/make_logo.py).
  * backs/*.txt - the card backs, 15x21 (a PNG of the same name overrides).

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
# Letters used in tools/art/*.txt. '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16

# The art sheets' order -> the game's (Klondike.h): c d s h.
GLYPH_ORDER = [0, 1, 3, 2]                                 # suits.txt is c d h s
SUIT_ORDER = [3, 1, 2, 0]                                  # the pip sheets are h d s c
# The card backs with art, in the order of the deck screen (after the two
# weaves, which are drawn in code). Must match BACK_NAME in CardArt.cpp.
BACKS = ["robot", "roses", "castle", "island", "fish", "shell", "cherry", "dice", "lucky", "chip"]
COURT_ROWS = 11                                            # the small card shows the bust


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
    """tools/art/<name>.txt: palette letters, one row per line; '#' starts a comment line."""
    rows = [ln.rstrip("\n") for ln in (artlib.art(HERE, f"{name}.txt")).read_text().splitlines()
            if ln and not ln.startswith("#")]
    w = max(len(r) for r in rows)
    return [[TRANSPARENT if ch in " ." else LETTER[ch] for ch in r.ljust(w)] for r in rows]


def load_sheet(name, sizes=None):
    """tools/art/<name>.txt with several glyphs per line separated by spaces."""
    rows = [ln.split() for ln in (artlib.art(HERE, f"{name}.txt")).read_text().splitlines()
            if ln.strip() and not ln.startswith("#")]
    imgs = []
    for i in range(len(rows[0])):
        img = [[TRANSPARENT if ch in "." else LETTER[ch] for ch in r[i]] for r in rows]
        if sizes:
            w, h = sizes[i] if isinstance(sizes, list) else sizes
            assert len(img) == h and all(len(r) == w for r in img), f"{name} glyph {i}: expected {w}x{h}"
        imgs.append(img)
    return imgs


def solid(img):
    return [[1 if v != TRANSPARENT else 0 for v in row] for row in img]


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
            out.append(sum(0x80 >> i for i in range(8) if x0 + i < len(row) and row[x0 + i]))
    return out


def pack_span1(bits):
    """Solid 1 bpp shape -> w, h, then per row: n, then (x0, len) pairs. Drawn as hlines."""
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
    """Colour image -> w, h, then per row: n, then n bytes of (len-1)<<4 | colour
    (sprite4 in the RPGame library's rpgame/Draw.cpp). Transparent runs use
    colour 15, which is skipped, so the art never draws FX_B. Trailing
    transparency is implicit."""
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
    PREVIEW.mkdir(parents=True, exist_ok=True)
    im.resize((w * scale, h * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


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
               "// Suit glyphs by Press Play On Tape (Apache-2.0), via CHBlackjack; card\n"
               "// ranks, pips and court portraits from CHBlackjack; the glove from\n"
               "// CHChess. See NOTICE.\n")
        OUT_H.parent.mkdir(parents=True, exist_ok=True)
        OUT_H.write_text(hdr + "#pragma once\n#include <stdint.h>\n\n" + "\n".join(self.h) + "\n")
        OUT_C.write_text(hdr + '#include "Assets.h"\n\n' + "\n".join(self.c) + "\n")


def main():
    o = Out()

    suits = load_sheet("suits", (5, 6))
    o.array("SUIT_SMALL", sum((pack_cols(solid(suits[g])) for g in GLYPH_ORDER), []),
            comment="suit glyphs c d s h, 5 columns each, bit 0 = top (6 rows)")

    sheet = load_sheet("ranks", [(7 if r == 9 else 5, 7) for r in range(13)])
    cols, widths = [], []
    for img in sheet:
        w = len(img[0])
        cols += pack_cols(solid(img)) + [0] * (7 - w)
        widths.append(w)
    o.array("RANK_GLYPH", cols, comment="ranks A..K, 7 column slots each, bit 0 = top")
    o.array("RANK_WIDTH", widths)

    for name, size in [("pip9", 9)]:
        imgs = load_sheet(name, (size, size))
        spans, idx = [], []
        for s in SUIT_ORDER:
            idx.append(len(spans))
            spans += pack_span1(solid(imgs[s]))
        o.array(name.upper(), spans, comment=f"{size}x{size} suit pips c d s h as row spans")
        o.array(name.upper() + "_AT", idx, "uint16_t")

    courts = load_sheet("court", (14, 18))
    for img, nm in zip(courts, ["JACK", "QUEEN", "KING"]):
        o.array(f"BUST_{nm}", pack_span4(img[:COURT_ROWS]), comment=f"{nm.title()}, the top {COURT_ROWS} rows (the small card)")
        preview(f"court_{nm.lower()}", img, bg=1)

    for nm in BACKS:
        png = ART / "backs" / f"{nm}.png"
        img = load_png(png) if png.exists() else load_art(f"backs/{nm}")
        assert len(img) == 21 and all(len(r) == 15 for r in img), f"backs/{nm}: expected 15x21"
        assert all(v != TRANSPARENT for r in img for v in r), f"backs/{nm}: no transparent pixels"
        o.array(f"BACK_{nm.upper()}", pack_span4(img), comment=f"card back '{nm}', 15x21, span4")
        preview(f"back_{nm}", img, 8)

    # The title lettering (tools/art/logo.txt), 1 bpp rows.
    rows = [ln.rstrip() for ln in (artlib.art(HERE, "logo.txt")).read_text().splitlines() if ln and not ln.startswith("# ")]
    w = max(len(r) for r in rows)
    bits = [[1 if ch == "#" else 0 for ch in r.ljust(w)] for r in rows]
    o.array("LOGO", pack_rows1(bits), comment=f"the title lettering, {w}x{len(bits)}, MSB-first rows")
    o.const("LOGO_W", w)
    o.const("LOGO_H", len(bits))
    preview("logo", [[1 if b else TRANSPARENT for b in r] for r in bits], 6, bg=0)

    png = artlib.art(HERE, "hand.png")
    hand = load_png(png) if png.exists() else load_art("hand")
    hand = hand[::-1]            # drawn pointing down; here it points up, from under a card
    o.array("HAND", pack_span4(hand), comment="the pointing glove (CHChess), turned over: span4, fingertip on the top row")
    tip = [x for x, v in enumerate(hand[0]) if v != TRANSPARENT]
    o.const("HAND_TIP", (tip[0] + tip[-1]) // 2)
    preview("hand", hand, 8)

    o.write()
    print(f"wrote {OUT_H.relative_to(ROOT)} and {OUT_C.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
