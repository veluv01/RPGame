"""The game's sprites as one sheet to edit (Photoshop or anything that keeps
an indexed PNG's colour table), and back.

    python tools/sheet.py export [SHEET]      # write tools/art/sheet.png (+ sheet_preview.png, 4x)
    python tools/sheet.py import [SHEET]      # read it back, then rebuild the game's assets

The sheet is an indexed PNG on the game's 16-colour palette, index 16
transparent. Paint only with the colour table's colours.

Each sprite has a cell (CELL x CELL, its name above it) and stands on the
middle of its cell's bottom edge; a wide one (the logo) has a row to itself
under the grid. Redraw it anywhere inside the cell, at any size that fits:
the import trims it to what you painted. A sprite that changed is written to
tools/art/<name>.png, which from then on replaces the letters in
tools/art/sprites.txt.

What the game expects of each:
  TOKEN_*   the four fruit, one per seat. They stand on their bottom row,
            centred. Drawn at the board's resting size; the camera doubles
            them when it whips in.
  HOUSE,    stand on a street's colour band, up to four houses side by side
  HOTEL     (they overlap at the resting size).
  CARD_DECK the two card stacks on the felt. GOLD is the back's colour: the
            Community Chest's deck is the same art with GOLD turned BLUE.
  DIE       a die with no pips: the game draws them on top, turning with it.
            Keep it 13 x 13.
  ICON_*    flat decals on the special tiles, centred. ICON_JAIL with its
            SILVER turned RED is Go To Jail.
  HAND      the glove; a CPU's has GOLD and WOOD (the cuff) turned RED and
            WINE. The fingertip is the middle of the bottom row.
  LOGO      the title's lettering, in any one colour (only its shape is
            kept): the game adds the gold-to-wood fill, outline and shadow,
            and prints it on the board's plaque. Up to 120 x 16.

Palette: the swatch lists the 16 colours. FX_A and FX_B are animated in the
game (placeholders here; FX_B cannot be used in sprites at all - it is the
sprite format's transparent). Colours are read by value (editors may reorder
the colour table); a colour that isn't the game's is an error - changing the
palette itself is a code change.
"""
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from assets import ART, PALETTE, TRANSPARENT, load_sprites, save_png  # noqa: E402

ROOT = HERE.parent
SHEET = ART / "sheet.png"
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]

# Layout, in sheet pixels.
CELL, COLS = 22, 6
LABEL = 8                       # room above a cell for its name
X0, Y0 = 4, 4
# FX_A/FX_B are animated in the game; distinct placeholders keep the colour
# table free of duplicates.
FX_SHOW = {14: 0xF0F, 15: 0xFE8}
SW_STEP = 8


def table_rgb(i):
    c = FX_SHOW.get(i, PALETTE[i])
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


def font35():
    """The 3x5 font (the RPGame library's, in
    platform/board/arduino/RPGame/libraries/RPGame/src/rpgame/Draw.cpp), for the labels: one glyph
    (3 column bytes) per character from '!' to 'z'; blank = none."""
    draw = (next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "libraries") / "RPGame" / "src" / "rpgame" / "Draw.cpp"
    src = re.sub(r"//[^\n]*", "", draw.read_text())
    body = src[src.index("FONT35[FONT35_LAST - FONT35_FIRST + 1][3] = {"):]
    body = body[:body.index("};")]
    return [tuple(int(v, 16) for v in g) for g in re.findall(r"\{(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+)\}", body)]


FONT = font35()


def text(px, x, y, s, c):
    for ch in s:
        g = FONT[ord(ch) - 33] if 33 <= ord(ch) <= 122 else (0, 0, 0)
        for col in range(3):
            for row in range(6):
                if g[col] >> row & 1:
                    px[x + col, y + row] = c
        x += 4


def layout(sprites):
    """-> {name: (x, y, w, h)} cells, and the sheet's (width, height, swatch x).
    Sprites that fit a cell fill the grid; wider ones follow, a row each."""
    cells, k = {}, 0
    wide = []
    for name, img in sprites.items():
        if len(img[0]) > CELL:
            wide.append(name)
            continue
        cells[name] = (X0 + (k % COLS) * (CELL + 2), Y0 + (k // COLS) * (CELL + LABEL + 2) + LABEL, CELL, CELL)
        k += 1
    y = Y0 + ((k + COLS - 1) // COLS) * (CELL + LABEL + 2)
    grid_w = COLS * (CELL + 2) - 2
    for name in wide:
        cells[name] = (X0, y + LABEL, grid_w, CELL)
        y += CELL + LABEL + 2
    sw_x = X0 + grid_w + 6
    return cells, (sw_x + 48, max(y + 2, Y0 + 16 * SW_STEP + 2), sw_x)


def short(name):
    """A sprite's name as its cell's label (five letters fit)."""
    special = {"TOKEN_DIE": "T.DIE", "DIE": "DICE", "CARD_DECK": "DECK"}
    return special.get(name, name.replace("TOKEN_", "").replace("ICON_", "I.")[:5])


def export(path):
    sprites = load_sprites()
    cells, (w, h, sw_x) = layout(sprites)
    im = Image.new("P", (w, h), TRANSPARENT)
    pal = []
    for i in range(16):
        pal += list(table_rgb(i))
    pal += [128, 128, 128]                   # 16: transparent
    im.putpalette(pal + [0] * (768 - len(pal)))
    px = im.load()
    for name, img in sprites.items():
        sw, sh = len(img[0]), len(img)
        x, y, cw, ch = cells[name]
        if sw > cw or sh > ch:
            raise SystemExit(f"{name}: {sw}x{sh} does not fit its {cw}x{ch} cell")
        text(px, x, y - LABEL + 1, short(name), 0)
        # The cell's floor: a line under it, so the bottom edge is plain to see.
        for i in range(cw):
            px[x + i, y + ch] = 5
        for j, row in enumerate(img):
            for i, c in enumerate(row):
                if c != TRANSPARENT:
                    px[x + (cw - sw) // 2 + i, y + ch - sh + j] = c
    for i in range(16):
        y = Y0 + i * SW_STEP
        for dy in range(6):
            for dx in range(6):
                px[sw_x + dx, y + dy] = i
        text(px, sw_x + 8, y, f"{i:>2} {NAMES[i].replace('_', ' ')}", 0)
    im.info["transparency"] = TRANSPARENT
    im.save(path, transparency=TRANSPARENT)
    # A 4x preview on a mid grey, to look at.
    pv = im.convert("RGBA")
    bg = Image.new("RGBA", pv.size, (96, 96, 104, 255))
    bg.alpha_composite(pv)
    bg.convert("RGB").resize((w * 4, h * 4), Image.NEAREST).save(path.with_name(path.stem + "_preview.png"))
    print(f"exported {path} ({w}x{h}) and its 4x preview")


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


def crop(img):
    """Trim to the opaque pixels, or None if empty."""
    ys = [y for y, row in enumerate(img) if any(v != TRANSPARENT for v in row)]
    xs = [x for x in range(len(img[0])) if any(row[x] != TRANSPARENT for row in img)]
    if not ys:
        return None
    return [row[xs[0]:xs[-1] + 1] for row in img[ys[0]:ys[-1] + 1]]


def import_(path):
    sprites = load_sprites()
    pix = read_indices(path)
    cells, (w, h, _) = layout(sprites)
    if len(pix) < h or len(pix[0]) < w:
        raise SystemExit(f"{path}: expected at least {w}x{h} (the exported layout)")
    changed = 0
    for name, old in sprites.items():
        x, y, cw, ch = cells[name]
        img = crop([row[x:x + cw] for row in pix[y:y + ch]])
        if img is None:
            raise SystemExit(f"{name}: its cell is empty")
        if any(15 in row for row in img):
            raise SystemExit(f"{name}: uses FX_B, which sprites cannot (it is their transparent colour)")
        if name == "DIE" and (len(img), len(img[0])) != (13, 13):
            raise SystemExit("DIE must stay 13 x 13 (the game draws the pips on it)")
        if img != crop(old):                     # (margins in the letters do not count)
            save_png(ART / f"{name.lower()}.png", img)
            print(f"art: {name} -> tools/art/{name.lower()}.png ({len(img[0])}x{len(img)})")
            changed += 1
    if not changed:
        print("no sprite changed")
    subprocess.run([sys.executable, str(HERE / "assets.py")], check=True)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("export", "import"):
        raise SystemExit(__doc__)
    path = Path(sys.argv[2]) if len(sys.argv) > 2 else SHEET
    (export if sys.argv[1] == "export" else import_)(path)


if __name__ == "__main__":
    main()
