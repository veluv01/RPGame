"""Preview the title logo (tools/art/logo.txt) the way the game draws it.

The logo is 1 bpp text art ('#' set, '.' clear). It goes through a Mask
with BJ's title ramp (FX_B rows < 3, GOLD to row 11, WOOD below), INK
outline and WINE shadow, onto BJ's feltBackdrop. For comparison it also
renders CHBlackjack's own "BlackJack" LOGO (decoded from its Assets.cpp)
and CHChess-style title35("ROULETTE") at scale 3.

Output: out/logo/*.png
    roulette_3x.png, roulette_6x.png   the new logo on the felt
    blackjack_3x.png, blackjack_6x.png BJ's logo, drawn the same way
    title35_3x.png                     title35("ROULETTE", scale 3)
    compare_3x.png                     BJ | new logo | title35, side by side
    strip_8x.png                       both logos cropped, 8x, for stroke weight
    bits_8x.png                        the raw 1 bpp bitmap with a pixel grid
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[10] / "tools"))   # the repository's tools/: pixkit
import pixkit as pk  # noqa: E402

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "out" / "logo"
LOGO_TXT = HERE / "art" / "logo.txt"
BJ_ASSETS = HERE.parents[1] / "CHBlackjack/src/assets/Assets.cpp"      # the sibling game (examples/Games/)


def load_logo(path=LOGO_TXT):
    rows = [ln.rstrip() for ln in path.read_text().splitlines() if ln.strip()]
    bad = {ch for r in rows for ch in r} - set("#.")
    assert not bad, f"{path.name}: unexpected characters {bad}"
    w = max(len(r) for r in rows)
    return [r.ljust(w, ".") for r in rows]


def load_bj_logo():
    """CHBlackjack's LOGO: 104x14, MSB-first rows."""
    text = BJ_ASSETS.read_text(encoding="utf-8")
    body = text[text.index("const uint8_t LOGO["):]
    body = body[body.index("{") + 1:body.index("};")]
    data = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", body)]
    w, h = 104, 14
    stride = (w + 7) // 8
    assert len(data) == stride * h, len(data)
    return ["".join("#" if data[y * stride + x // 8] & (0x80 >> (x & 7)) else "."
                    for x in range(w)) for y in range(h)]


def ink_width(rows):
    """Columns from the first to the last set pixel."""
    cols = [x for r in rows for x, ch in enumerate(r) if ch == "#"]
    return min(cols), max(cols)


def felt_backdrop(fb):
    """CHBlackjack Screens.cpp feltBackdrop()."""
    fb.clear(pk.FELT)
    fb.dither(0, 0, 128, 6, pk.FELT_DK, 0)
    fb.dither(0, 122, 128, 6, pk.FELT_DK, 1)
    fb.dither(0, 0, 6, 128, pk.FELT_DK, 0)
    fb.dither(122, 0, 6, 128, pk.FELT_DK, 1)
    fb.rect(2, 2, 124, 124, pk.GOLD)


def bj_ramp(h):
    """BJ's title ramp: for (i < 16) ramp[i] = i < 3 ? FX_B : i < 12 ? GOLD : WOOD."""
    return [pk.FX_B if i < 3 else (pk.GOLD if i < 12 else pk.WOOD) for i in range(max(h, 16))]


def draw_logo(fb, rows, x, y):
    m = pk.Mask(len(rows[0]), len(rows))
    m.blit(rows)
    m.draw(fb, x, y, pk.GOLD, pk.INK, pk.WINE, bj_ramp(len(rows)))


def logo_screen(rows, y=8, x=None):
    fb = pk.FB()
    felt_backdrop(fb)
    if x is None:                       # centre the inked columns
        a, b = ink_width(rows)
        x = 64 - (a + b + 1) // 2
    draw_logo(fb, rows, x, y)
    return fb


def title35_screen(y=8):
    fb = pk.FB()
    felt_backdrop(fb)
    pk.title35(fb, "ROULETTE", y, 3, pk.FX_B, pk.GOLD, pk.WOOD, pk.WINE, 13)
    return fb


def side_by_side(images, labels, gap=12, pad=22):
    w = sum(im.width for im in images) + gap * (len(images) + 1)
    h = max(im.height for im in images) + pad + gap
    out = Image.new("RGB", (w, h), (24, 24, 28))
    d = ImageDraw.Draw(out)
    x = gap
    for im, label in zip(images, labels):
        d.text((x, 6), label, fill=(220, 220, 220))
        out.paste(im, (x, pad))
        x += im.width + gap
    return out


def bits_image(rows, scale=8):
    w, h = len(rows[0]), len(rows)
    im = Image.new("RGB", (w * scale + 1, h * scale + 1), (40, 40, 48))
    d = ImageDraw.Draw(im)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            c = (250, 210, 60) if ch == "#" else ((28, 28, 34) if (x // 8) % 2 else (20, 20, 24))
            d.rectangle([x * scale + 1, y * scale + 1, x * scale + scale - 1, y * scale + scale - 1], fill=c)
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    new = load_logo()
    bj = load_bj_logo()
    a, b = ink_width(new)
    print(f"logo.txt: {len(new[0])}x{len(new)} (inked {b - a + 1} px wide), "
          f"{(len(new[0]) + 7) // 8 * len(new)} bytes at 1 bpp")

    fb_new = logo_screen(new)
    fb_bj = logo_screen(bj, x=12)        # where BJ's titleRender puts it
    fb_t35 = title35_screen()
    fb_new.save(OUT / "roulette_3x.png", 3)
    fb_new.save(OUT / "roulette_6x.png", 6)
    fb_bj.save(OUT / "blackjack_3x.png", 3)
    fb_bj.save(OUT / "blackjack_6x.png", 6)
    fb_t35.save(OUT / "title35_3x.png", 3)

    side_by_side([fb_bj.image(3), fb_new.image(3), fb_t35.image(3)],
                 ["CHBlackjack LOGO (reference)", "new 'Roulette' logo (logo.txt)",
                  "title35('ROULETTE', scale 3)"]).save(OUT / "compare_3x.png")

    # Both logos cropped from their screens, stacked, at 8x.
    crops = [fb.image(8).crop((0, 4 * 8, 128 * 8, 26 * 8)) for fb in (fb_bj, fb_new)]
    strip = Image.new("RGB", (crops[0].width, sum(c.height for c in crops) + 8), (24, 24, 28))
    strip.paste(crops[0], (0, 0))
    strip.paste(crops[1], (0, crops[0].height + 8))
    strip.save(OUT / "strip_8x.png")

    bits_image(new).save(OUT / "bits_8x.png")


if __name__ == "__main__":
    main()
