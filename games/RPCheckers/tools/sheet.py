"""The chip art as one sprite sheet to edit (Photoshop or anything that
keeps an indexed PNG's colour table), and back.

    python tools/sheet.py export [SHEET]      # write tools/art/sheet.png (+ sheet_preview.png, 4x)
    python tools/sheet.py import [SHEET]      # read it back, then rebuild the game's assets

The sheet is an indexed PNG on the game's 16-colour palette, index 16
transparent. Paint only with the colour table's colours.

  MASTER  the art itself: CHIP (a man, seen from the table's edge), TOP
          (the chip from above) and the pointing glove, in the neutral
          tones both sides are dressed from (face WHITE, edge SILVER,
          inserts BLUE, INK outline, the crown GOLD). Change shapes and
          shading here. The chip stands with its base centre on a fixed
          point of its cell (9, 17 in the cell) and TOP sits where it was
          exported; the game draws them from there, so they may grow right
          and down from where they were exported, not left or up. Keep the
          glove's fingertip where it is too.
  WHITE,  the chips as each side shows them: MASTER through the palette
  BLACK   swap. Under CHIP and TOP is the man; recolour it to change the
          swap - a colour of MASTER must become one colour on a side (the
          import takes each MASTER colour's most common colour and reports
          the rest). Or redraw the men outright (clear MASTER's chips, or
          change their shapes here): the import then rebuilds the art from
          these two rows - each White/Black pair of colours becomes one
          tone of the shared art - so both sides' shapes must match, and at
          most 16 pairs. The crown is GOLD in MASTER and takes the face's
          colour on a man, so it cannot be seen in these rows: a rebuild
          keeps the crown of the art there was if the chip's shape is
          unchanged, else the crown has to be drawn again in MASTER.
          The KING cells beside each man are previews only, never read
          back: a king is the chip twice, the upper one 3 pixels higher (2
          for TOP) with its crown left GOLD. Re-export to refresh them.
  SWAP    the same swap as a key: per palette colour, what it becomes on
          White and on Black. Editing a square changes the swap too (the
          chip rows win if both changed).

The import writes tools/art/chip.png and chiptop.png (only those that
changed; a .png takes over from the .txt beside it), the glove to the
shared tools/art/common/hand.png (every game that draws it gets the change), and
tools/art/sides.txt, then runs tools/assets.py.

Palette: the swatch lists the 16 colours. FX_A and FX_B are animated in
the game (placeholders here); FELT_DK, FELT and FELT_LT follow the board
colour option. Colours are read by value (editors may reorder the colour
table); a colour that isn't the game's is an error - changing the palette
itself is a code change.
"""
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import assets  # noqa: E402
from assets import (ART, CHIP_ANCHOR, CROWN, NAMES, PALETTE, TRANSPARENT, king_maps,  # noqa: E402
                    load_hand, load_sides, load_sprite, save_png, save_sides)

INK = 0

ROOT = HERE.parent
SHEET = ART / "sheet.png"

# Layout, in sheet pixels.
CW, CH = 18, 24                 # a cell
AX, AY = 9, 17                  # where the chip's base centre sits in its cell
LX = 30                         # left of the cells (labels before it)
ROW_Y = [12, 12 + CH, 12 + 2 * CH]         # MASTER, WHITE, BLACK
KEY_Y = 12 + 3 * CH + 10                   # SWAP key: three rows of squares
KEY_STEP, KEY_SQ = 7, 6
# The chips: name, label, column (its king preview is the next one), the
# point of the art that sits on AX, AY (the game draws from fixed offsets:
# assets.CHIP_ANCHOR; chiptop at x - 4, y - 6 in Stage.cpp), and
# how much higher the upper chip of a king is drawn.
CHIPS = [("chip", "CHIP", 0, CHIP_ANCHOR, 3), ("chiptop", "TOP", 2, (4, 6), 2)]
HAND_COL = 4
COLS = 5
SW_X = LX + max(COLS * CW, 16 * KEY_STEP) + 8          # palette swatch
SW_Y, SW_STEP = 12, 8
W, H = SW_X + 48, max(KEY_Y + 3 * KEY_STEP + 4, SW_Y + 16 * SW_STEP + 2)
# FX_A/FX_B are animated in the game; distinct placeholders keep the colour
# table free of duplicates.
FX_SHOW = {14: 0xF0F, 15: 0xFE8}


def table_rgb(i):
    c = FX_SHOW.get(i, PALETTE[i])
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


# ---------------------------------------------------------------------------
# The game's 3x5 font (the RPGame library's rpgame/Draw.cpp) for the labels
# ---------------------------------------------------------------------------
DRAW_CPP = (next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "libraries") / "RPGame" / "src" / "rpgame" / "Draw.cpp"


def font35():
    """FONT35's glyphs, one for each character from '!' on."""
    src = re.sub(r"//[^\n]*", "", DRAW_CPP.read_text())
    body = src[src.index("FONT35[FONT35_LAST - FONT35_FIRST + 1][3] = {"):]
    body = body[:body.index("};")]
    return [tuple(int(v, 16) for v in g) for g in re.findall(r"\{(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+)\}", body)]


def text(px, x, y, s, c):
    for ch in s:
        g = ord(ch) - ord("!")
        if 0 <= g < len(FONT):
            for col in range(3):
                for row in range(6):
                    if FONT[g][col] >> row & 1:
                        px[x + col, y + row] = c
        x += 4


FONT = font35()


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------
def blit(px, img, x, y):
    for j, row in enumerate(img):
        for i, c in enumerate(row):
            if c != TRANSPARENT:
                px[x + i, y + j] = c


def remapped(img, m):
    return [[c if c == TRANSPARENT else m[c] for c in row] for row in img]


def fits(img, ax, ay, lift, name):
    if AX - ax < 0 or AY - ay - lift < 0 or AX - ax + len(img[0]) > CW or AY - ay + len(img) > CH:
        raise SystemExit(f"{name}: {len(img[0])}x{len(img)} with its base at {ax},{ay} does not fit a {CW}x{CH} cell")


def export(path):
    im = Image.new("P", (W, H), TRANSPARENT)
    pal = []
    for i in range(16):
        pal += list(table_rgb(i))
    pal += [128, 128, 128]                   # 16: transparent
    im.putpalette(pal + [0] * (768 - len(pal)))
    px = im.load()
    maps = load_sides()
    for r, label in enumerate(["MASTER", "WHITE", "BLACK"]):
        text(px, 2, ROW_Y[r] + 12, label, 0)
    kings = king_maps(maps)
    for name, label, c, (ax, ay), lift in CHIPS:
        img = load_sprite(name)
        fits(img, ax, ay, lift, name)
        x = LX + c * CW + AX - ax
        text(px, LX + c * CW + 1, 4, label, 0)
        blit(px, img, x, ROW_Y[0] + AY - ay)
        # The king beside it: a preview, not read back.
        text(px, LX + (c + 1) * CW + 1, 4, "KING", 0)
        for j, word in enumerate(("VIEW", "ONLY")):
            text(px, LX + (c + 1) * CW + 1, ROW_Y[0] + 5 + 7 * j, word, 0)
        for r in (1, 2):
            y = ROW_Y[r] + AY - ay
            blit(px, remapped(img, maps[r - 1]), x, y)
            blit(px, remapped(img, maps[r - 1]), x + CW, y)
            blit(px, remapped(img, kings[r - 1]), x + CW, y - lift)
    hand = load_hand()
    text(px, LX + HAND_COL * CW + 1, 4, "HAND", 0)
    blit(px, hand, LX + HAND_COL * CW + AX - len(hand[0]) // 2, ROW_Y[0] + AY - len(hand) + 1)
    # The swap key.
    text(px, 2, KEY_Y - 8, "SWAP", 0)
    for r, label in enumerate(["ART", "WHITE", "BLACK"]):
        text(px, 8 if r == 0 else 2, KEY_Y + r * KEY_STEP, label, 0)
        for i in range(16):
            c = i if r == 0 else maps[r - 1][i]
            for dy in range(KEY_SQ):
                for dx in range(KEY_SQ):
                    px[LX + i * KEY_STEP + dx, KEY_Y + r * KEY_STEP + dy] = c
    # The palette swatch.
    for i in range(16):
        y = SW_Y + i * SW_STEP
        for dy in range(6):
            for dx in range(6):
                px[SW_X + dx, y + dy] = i
        text(px, SW_X + 8, y, f"{i:>2} {NAMES[i].replace('_', ' ')}", 0)
    im.info["transparency"] = TRANSPARENT
    im.save(path, transparency=TRANSPARENT)
    # A 4x preview on a mid grey, to look at.
    pv = im.convert("RGBA")
    bg = Image.new("RGBA", pv.size, (96, 96, 104, 255))
    bg.alpha_composite(pv)
    bg.convert("RGB").resize((W * 4, H * 4), Image.NEAREST).save(path.with_name(path.stem + "_preview.png"))
    print(f"exported {path} ({W}x{H}) and its 4x preview")


# ---------------------------------------------------------------------------
# Import
# ---------------------------------------------------------------------------
def read_indices(path):
    """-> 2D game palette indices, TRANSPARENT where clear. Matched by colour:
    editors may save the colour table in their own order."""
    lut = {table_rgb(i): i for i in reversed(range(16))}     # GOLD before FX_B
    im = Image.open(path).convert("RGBA")
    out, bad = [], {}
    for y in range(im.height):
        row = []
        for x in range(im.width):
            r, g, b, a = im.getpixel((x, y))
            if a < 128:
                row.append(TRANSPARENT)
            elif (r, g, b) in lut:
                row.append(lut[(r, g, b)])
            else:
                bad.setdefault((r, g, b), (x, y))
                row.append(TRANSPARENT)
        out.append(row)
    if bad:
        raise SystemExit("not palette colours: " + ", ".join(
            f"#{r:02X}{g:02X}{b:02X} at {xy}" for (r, g, b), xy in bad.items()) +
            " - paint with the sheet's colours (changing the palette itself is a code change)")
    return out


def cell(pix, c, r):
    x0, y0 = LX + c * CW, ROW_Y[r]
    return [row[x0:x0 + CW] for row in pix[y0:y0 + CH]]


def crop(img):
    """Trim to the opaque pixels -> (rows, left, top), or None if empty."""
    ys = [y for y, row in enumerate(img) if any(v != TRANSPARENT for v in row)]
    xs = [x for x in range(len(img[0])) if any(row[x] != TRANSPARENT for row in img)]
    if not ys:
        return None
    return [row[xs[0]:xs[-1] + 1] for row in img[ys[0]:ys[-1] + 1]], xs[0], ys[0]


def place(img, ax, ay):
    """A chip as exported into its cell."""
    out = [[TRANSPARENT] * CW for _ in range(CH)]
    for j, row in enumerate(img):
        for i, v in enumerate(row):
            if v != TRANSPARENT:
                out[AY - ay + j][AX - ax + i] = v
    return out


def opaque(img):
    return [[v != TRANSPARENT for v in row] for row in img]


def write_sprite(name, art, ax, ay):
    """A cell of art -> tools/art/<name>.png, its anchor where the game expects it."""
    img, left, top = crop(art)
    x0, y0 = AX - ax, AY - ay
    if left < x0 or top < y0:
        raise SystemExit(f"{name}: drawn {max(x0 - left, 0)} px left of, {max(y0 - top, 0)} px above where it was "
                         f"exported - the game draws it from that corner (base centre {ax},{ay}), so it can only "
                         f"grow right and down; moving the corner is a code change")
    img = [row[x0:left + len(img[0])] for row in art[y0:top + len(img)]]
    if img != load_sprite(name):
        save_png(ART / f"{name}.png", img)
        print(f"art: {name} -> tools/art/{name}.png")


def import_(path):
    pix = read_indices(path)
    if len(pix) < H or len(pix[0]) < W:
        raise SystemExit(f"{path}: expected at least {W}x{H} (the exported layout)")
    notes = []
    old = load_sides()
    cells = [[cell(pix, c, r) for r in range(3)] for _, _, c, _, _ in CHIPS]

    # Which rows are the art? MASTER, if every chip is there and both side
    # rows keep its shapes; else the WHITE and BLACK rows themselves (the
    # men; the king cells are previews).
    from_sides = any(not crop(m) or opaque(w) != opaque(m) or opaque(b) != opaque(m) for m, w, b in cells)
    if from_sides:
        new = rebuild_from_sides(cells, notes)
    else:
        new = [list(m) for m in old]
        swap_from_rows(cells, old, new, notes)
        swap_from_key(pix, old, new, notes)
        for c, (name, _, _, (ax, ay), _) in enumerate(CHIPS):
            write_sprite(name, cells[c][0], ax, ay)

    for side, sname in ((0, "White"), (1, "Black")):
        for i in range(16):
            if new[side][i] != old[side][i]:
                print(f"swap: {sname}'s {NAMES[i]} -> {NAMES[new[side][i]]} (was {NAMES[old[side][i]]})")
    if new != old:
        save_sides(new)

    # The glove.
    got = crop(cell(pix, HAND_COL, 0))
    if not got:
        notes.append("MASTER hand: empty cell - left as it was")
    elif got[0] != load_hand():
        # The glove is shared art: save it back where it came from (through
        # artlib, the repository's tools/art/common/), not a copy here that
        # would leave CHCheckers' apart from the other games'.
        dst = assets.artlib.art(HERE, "hand.png")
        save_png(dst, got[0])
        print(f"art: hand -> {dst}")
        print("note: the glove is shared: run tools/assets.py in every game that uses it "
              "(grep -l hand.png in the games' tools/assets.py), then check their frames")

    for n in notes:
        print("note:", n)
    subprocess.run([sys.executable, str(HERE / "assets.py")], check=True)


def swap_from_rows(cells, old, new, notes):
    """MASTER is the art: what each of its colours became on each side (the
    most common colour; the rest reported)."""
    for side, sname in ((0, "WHITE"), (1, "BLACK")):
        votes = {}
        for c in range(len(CHIPS)):
            m, got = cells[c][0], cells[c][side + 1]
            for y in range(CH):
                for x in range(CW):
                    if m[y][x] != TRANSPARENT:
                        v = votes.setdefault(m[y][x], {})
                        v[got[y][x]] = v.get(got[y][x], 0) + 1
        for m, v in votes.items():
            best = max(v, key=v.get)
            stray = sum(v.values()) - v[best]
            if stray:
                notes.append(f"{sname}: {NAMES[m]} became {NAMES[best]} in {v[best]} pixels, something else "
                             f"in {stray} - one colour per art colour on a side; kept {NAMES[best]}")
            new[side][m] = best


def swap_from_key(pix, old, new, notes):
    """The SWAP key's squares count where the chips left a colour as it was."""
    for side in (0, 1):
        y = KEY_Y + (side + 1) * KEY_STEP + KEY_SQ // 2
        for i in range(16):
            v = pix[y][LX + i * KEY_STEP + KEY_SQ // 2]
            if v == TRANSPARENT or v == old[side][i]:
                continue
            if new[side][i] == old[side][i]:
                new[side][i] = v
            elif new[side][i] != v:
                notes.append(f"{('WHITE', 'BLACK')[side]}: {NAMES[i]} - chips say {NAMES[new[side][i]]}, "
                             f"the key says {NAMES[v]}; took the chips")


# Free slots for art tones White and Black both already use, most fitting
# first (CYAN and NAVY are not in the chip); GOLD late, it is the crown;
# felt and FX colours last.
SPARE = (13, 11, 5, 10, 1, 12, 6, 7, 9, 8, 2, 3, 4, 14, 15, 0)


def rebuild_from_sides(cells, notes):
    """The WHITE and BLACK rows are the art: every pixel's pair (White's
    colour, Black's) becomes one colour of the shared art, and the swap
    sends it to each. The pair drawn in INK on White keeps INK (the game
    recolours INK for its outline highlights); the others take White's
    colour, else Black's, else a free palette slot (never drawn as itself -
    the art is always drawn through the swap).

    The crown cannot be seen on a man (GOLD takes the face's colour on both
    sides), so these rows do not hold it: it is kept from the art there was
    where the chip's shape is unchanged and one pair of colours covers it."""
    pairs = {}
    for c, (name, *_) in enumerate(CHIPS):
        _, w, b = cells[c]
        if opaque(w) != opaque(b):
            n = sum(1 for y in range(CH) for x in range(CW) if (w[y][x] == TRANSPARENT) != (b[y][x] == TRANSPARENT))
            raise SystemExit(f"{name}: WHITE and BLACK differ in shape at {n} pixels - the two sides share one "
                             f"set of art, so their outlines must match")
        if not crop(w):
            raise SystemExit(f"{name}: no man in MASTER, WHITE or BLACK to take the art from")
        for y in range(CH):
            for x in range(CW):
                if w[y][x] != TRANSPARENT:
                    p = (w[y][x], b[y][x])
                    pairs[p] = pairs.get(p, 0) + 1
    if len(pairs) > 16:
        raise SystemExit(f"{len(pairs)} different White/Black colour pairs - the art has room for 16")
    # The crown of the art there was, where a chip still has its shape.
    crown, lost = {}, []
    for c, (name, _, _, (ax, ay), _) in enumerate(CHIPS):
        was = place(load_sprite(name), ax, ay)
        if not any(CROWN in row for row in was):
            continue
        if opaque(was) == opaque(cells[c][1]):
            crown[c] = [(x, y) for y in range(CH) for x in range(CW) if was[y][x] == CROWN]
        else:
            lost.append(name)
    under = {(cells[c][1][y][x], cells[c][2][y][x]) for c, at in crown.items() for x, y in at}
    if crown and (len(under) > 1 or len(pairs) > 15):
        lost += [CHIPS[c][0] for c in crown]
        crown = {}
    key, taken = {}, {CROWN} if crown else set()
    ink = [p for p in pairs if p[0] == INK]
    for p in sorted(ink, key=lambda p: -pairs[p])[:1]:
        key[p] = INK
        taken.add(INK)
    for p in sorted(pairs, key=lambda p: -pairs[p]):
        if p in key:
            continue
        for k in (p[0], p[1]) + SPARE:
            if k not in taken:
                key[p] = k
                taken.add(k)
                break
    new = [list(range(16)), list(range(16))]
    for (w, b), k in key.items():
        new[0][k], new[1][k] = w, b
    for (w, b), n in sorted(pairs.items(), key=lambda t: -t[1]):
        print(f"tone {NAMES[key[(w, b)]]:<8} White {NAMES[w]:<8} Black {NAMES[b]:<8} ({n} px)")
    if crown:
        new[0][CROWN], new[1][CROWN] = under.pop()
    for c, (name, _, _, (ax, ay), _) in enumerate(CHIPS):
        w, b = cells[c][1], cells[c][2]
        art = [[key[(w[y][x], b[y][x])] if w[y][x] != TRANSPARENT else TRANSPARENT for x in range(CW)]
               for y in range(CH)]
        for x, y in crown.get(c, ()):
            art[y][x] = CROWN
        write_sprite(name, art, ax, ay)
    notes.append("the art was rebuilt from the WHITE and BLACK rows (MASTER's chips were missing or "
                 "the side rows had new shapes); re-export to see it as MASTER")
    if crown:
        notes.append("the crown (GOLD) does not show on a man, so the side rows do not hold it: kept from the "
                     "art there was (" + ", ".join(CHIPS[c][0] for c in crown) + ")")
    if lost:
        notes.append("the crown (GOLD) does not show on a man and could not be kept (" + ", ".join(lost) +
                     ": new shape, or new colours across it) - kings have no crown until it is drawn in "
                     "MASTER again")
    return new


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args or args[0] not in ("export", "import"):
        raise SystemExit(__doc__)
    path = Path(args[1]) if len(args) > 1 else SHEET
    if args[0] == "export":
        export(path)
    else:
        import_(path)


if __name__ == "__main__":
    main()
