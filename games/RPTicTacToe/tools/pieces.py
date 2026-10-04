"""Render the pieces for the isometric tables (the art's starting point).

    python tools/pieces.py [--scale 8]    -> tools/art/pieces/*.png + .anchor, build/assets/pieces_sheet.png

The X is two crossed lacquered bars and the O a ring, both standing on the
felt and facing the player; GOBBLE's pieces are casino chips in three
sizes. They are modelled as signed-distance shapes and ray-marched at the
board's camera angle (CHChess's tools/pieces.py): the 2:1 tiles mean the
camera looks down at 30 degrees. Shading is quantised to the palette and
outlined in ink.

Each piece comes in two sizes: L for the 3x3 tables (40x20 tiles) and S for
the 5x5 ones (24x12). The PNGs in tools/art/pieces/ are the source of
truth for tools/assets.py and may be touched up by hand; rerunning this
overwrites them.
"""
import argparse
import math
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = HERE / "art" / "pieces"
PREVIEW = ROOT / "build" / "assets"

PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
INK, WHITE, FELT, SILVER, RED, WINE, GOLD, WOOD, BLUE, NAVY, SKIN, CYAN = 0, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13


def rgb(i):
    c = PALETTE[i]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


# Tones dark to light, then the glint.
TONES = {
    "red": [WINE, RED, RED, SKIN, WHITE],
    "blue": [NAVY, BLUE, BLUE, CYAN, WHITE],
}
TRIM = [WOOD, GOLD, GOLD, WHITE, WHITE]
THRESH = [0.36, 0.55, 0.86]

ELEV = math.radians(30.0)
D = np.array([0.0, math.cos(ELEV), -math.sin(ELEV)])     # view direction (into the screen)
R_AX = np.array([1.0, 0.0, 0.0])                          # screen right
U_AX = np.array([0.0, math.sin(ELEV), math.cos(ELEV)])    # screen up
LIGHT = np.array([-0.55, -0.6, 0.75]); LIGHT /= np.linalg.norm(LIGHT)
VIEW = -D
HALF = LIGHT + VIEW; HALF /= np.linalg.norm(HALF)


# ---------------------------------------------------------------------------
# Shapes: f(p) < 0 inside; p is (N, 3), z up, base on z = 0, units = pixels
# at the L size.
# ---------------------------------------------------------------------------
def bar(angle, length, half_w, half_d, cz, round_):
    """A rounded box in the x-z plane (facing the camera), turned by angle."""
    cs, sn = math.cos(angle), math.sin(angle)

    def f(p):
        x = p[:, 0]; z = p[:, 2] - cz
        a = cs * x + sn * z
        b = -sn * x + cs * z
        q = np.stack([np.abs(a) - length / 2 + round_, np.abs(b) - half_w + round_,
                      np.abs(p[:, 1]) - half_d + round_], 1)
        return np.linalg.norm(np.maximum(q, 0), axis=1) + np.minimum(q.max(axis=1), 0) - round_
    return f


def union(*fs):
    def f(p):
        return np.minimum.reduce([g(p) for g in fs])
    return f


def piece_x():
    h = 23.0
    L = h * math.sqrt(2) + 1.0
    shape = union(bar(math.pi / 4, L, 3.6, 3.4, h / 2, 1.4), bar(-math.pi / 4, L, 3.6, 3.4, h / 2, 1.4))
    # Clip the feet flat so it stands.
    return lambda p: np.maximum(shape(p), -p[:, 2]), (lambda p: np.zeros(len(p), bool)), h


def piece_o():
    R, r = 8.4, 3.0
    cz = R + r

    def f(p):
        qx = np.hypot(p[:, 0], p[:, 2] - cz) - R
        return np.hypot(qx, p[:, 1] * 0.8) - r
    return (lambda p: np.maximum(f(p), -p[:, 2] + 0.4)), (lambda p: np.zeros(len(p), bool)), 2 * cz


def chip(radius, height):
    """A casino chip lying flat: edge spots and a gold ring on its face."""
    def f(p):
        rho = np.hypot(p[:, 0], p[:, 1])
        q = np.stack([rho - radius + 0.8, np.abs(p[:, 2] - height / 2) - height / 2 + 0.8], 1)
        return np.linalg.norm(np.maximum(q, 0), axis=1) + np.minimum(q.max(axis=1), 0) - 0.8

    def gold(p):
        rho = np.hypot(p[:, 0], p[:, 1])
        ring = (p[:, 2] > height - 0.6) & (rho > radius * 0.52) & (rho < radius * 0.68)
        ang = np.arctan2(p[:, 1], p[:, 0])
        spots = (rho > radius - 1.2) & (np.cos(ang * 6) > 0.55) & (p[:, 2] < height - 0.6)
        return ring | spots
    return f, gold, height


# name -> (factory, tones)
PIECES = {
    "x": (piece_x, "red"),
    "o": (piece_o, "blue"),
    "chip0": (lambda: chip(6.5, 4.0), "red"),
    "chip1": (lambda: chip(9.5, 5.0), "red"),
    "chip2": (lambda: chip(12.5, 6.0), "red"),
}
SIZES = {"L": 1.0, "S": 0.62}
# The X and O are rendered 2 px smaller each way than their natural size at
# SIZES (the user's call: they sit inside their tiles, and a piece held over
# one reads as above it).
SHRINK = {"x": 2, "o": 2}
# The X and O held in a glove on the 3x3 tables spin: frames turned 45 and
# 90 degrees about the vertical (the game mirrors the first for 135; both
# shapes repeat every half turn).
SPIN = [45, 90]
WHICH = {"L":["x", "o", "chip0", "chip1", "chip2"], "S": ["x", "o", "chip0", "chip1", "chip2"]}


# ---------------------------------------------------------------------------
# Rendering (CHChess's ray-marcher)
# ---------------------------------------------------------------------------
def render(shape, gold, height, tones, size=1.0, ss=8):
    rmax = int(math.ceil(15 * size))
    w = 2 * rmax + 3
    top = int(math.ceil((height * math.cos(ELEV) + 8) * size)) + 3
    bot = int(math.ceil(8 * size)) + 2
    h = top + bot
    xs = ((np.arange(w * ss) + 0.5) / ss - w / 2) / size
    ys = ((np.arange(h * ss) + 0.5) / ss - top) / size
    X, Y = np.meshgrid(xs, ys)
    X = X.ravel(); Y = Y.ravel()
    origin = X[:, None] * R_AX[None, :] - Y[:, None] * U_AX[None, :] - D[None, :] * 80.0
    t = np.zeros(len(X))
    hit = np.zeros(len(X), bool)
    alive = np.ones(len(X), bool)
    prev = np.zeros(len(X))
    for _ in range(600):
        idx = np.nonzero(alive)[0]
        if not len(idx):
            break
        p = origin[idx] + D[None, :] * t[idx, None]
        d = shape(p)
        inside = d < 0.0
        hit[idx[inside]] = True
        alive[idx[inside]] = False
        out = idx[~inside]
        prev[out] = t[out]
        t[out] += np.maximum(0.06, 0.5 * d[~inside])
        alive &= ~(t > 160)
    hi_t = t.copy(); lo_t = prev.copy()
    hidx = np.nonzero(hit)[0]
    for _ in range(12):
        mid = 0.5 * (lo_t[hidx] + hi_t[hidx])
        d = shape(origin[hidx] + D[None, :] * mid[:, None])
        ins = d < 0
        hi_t[hidx[ins]] = mid[ins]
        lo_t[hidx[~ins]] = mid[~ins]
    t = hi_t
    p = origin + D[None, :] * t[:, None]
    e = 0.3
    n = np.zeros_like(p)
    for k in range(3):
        dv = np.zeros(3); dv[k] = e
        n[:, k] = shape(p + dv) - shape(p - dv)
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
    diff = np.clip(n @ LIGHT, 0, 1)
    rim = np.clip(1.0 - (n @ VIEW), 0, 1) ** 3
    spec = np.clip(n @ HALF, 0, 1) ** 24
    inten = 0.16 + 0.84 * diff + 0.15 * rim
    is_gold = gold(p)

    def pool(a):
        return a.reshape(h, ss, w, ss).mean(axis=(1, 3))
    cov = pool(hit.astype(float))
    ih = pool(np.where(hit, inten, 0)) / np.maximum(cov, 1e-6)
    sh = pool(np.where(hit, spec, 0)) / np.maximum(cov, 1e-6)
    gh = pool(np.where(hit & is_gold, 1.0, 0)) / np.maximum(cov, 1e-6)
    body = TONES[tones]
    img = np.full((h, w), -1, int)
    for y in range(h):
        for x in range(w):
            if cov[y, x] < 0.5:
                continue
            tt = TRIM if gh[y, x] >= 0.5 else body
            k = sum(ih[y, x] >= th for th in THRESH)
            c = tt[k]
            if sh[y, x] > 0.7:
                c = tt[4]
            img[y, x] = c
    # Despeckle: a lone pixel between two of the same colour takes theirs.
    for _ in range(2 if size >= 1 else 1):
        for y in range(h):
            for x in range(1, w - 1):
                a, b, c = img[y, x - 1], img[y, x], img[y, x + 1]
                if a >= 0 and a == c and b != a and b != INK:
                    img[y, x] = a
    out = img.copy()
    for y in range(h):
        for x in range(w):
            if img[y, x] >= 0:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and 0 <= yy < h and img[yy, xx] >= 0:
                    out[y, x] = INK
                    break
    ys_, xs_ = np.nonzero(out >= 0)
    y0, y1, x0, x1 = ys_.min(), ys_.max() + 1, xs_.min(), xs_.max() + 1
    anchor = (w // 2 - x0, top - y0)               # base centre within the crop
    return out[y0:y1, x0:x1], anchor


def to_png(img):
    h, w = img.shape
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            if img[y, x] >= 0:
                im.putpixel((x, y), rgb(img[y, x]) + (255,))
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=int, default=8, help="preview magnification")
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    tiles = []
    for sz, names in WHICH.items():
        for name in names:
            fn, tones = PIECES[name]
            shape, gold, height = fn()
            img, anchor = render(shape, gold, height, tones, SIZES[sz])
            k = 1.0
            if name in SHRINK:
                w0, h0 = img.shape[1], img.shape[0]
                while img.shape[1] > w0 - SHRINK[name] or img.shape[0] > h0 - SHRINK[name]:
                    k -= 0.01
                    img, anchor = render(shape, gold, height, tones, SIZES[sz] * k)
            frames = [(f"{name}_{sz.lower()}", img, anchor)]
            if name in SHRINK and sz == "L":
                for i, deg in enumerate(SPIN, 1):
                    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))

                    def turned(p, shape=shape, c=c, s=s):
                        q = p.copy()
                        q[:, 0] = c * p[:, 0] + s * p[:, 1]
                        q[:, 1] = -s * p[:, 0] + c * p[:, 1]
                        return shape(q)
                    fi, fa = render(turned, gold, height, tones, SIZES[sz] * k)
                    frames.append((f"{name}_{sz.lower()}{i}", fi, fa))
            for stem, im, an in frames:
                to_png(im).save(OUT / f"{stem}.png")
                (OUT / f"{stem}.anchor").write_text(f"{an[0]} {an[1]}\n")
                print(f"{stem}: {im.shape[1]}x{im.shape[0]} anchor {an}")
                tiles.append(im)
    pad = 4
    W = sum(t.shape[1] + pad for t in tiles) + pad
    H = max(t.shape[0] for t in tiles) + 2 * pad
    sheet = Image.new("RGB", (W, H), rgb(FELT))
    x = pad
    for im in tiles:
        p = to_png(im)
        sheet.paste(p, (x, pad), p)
        x += im.shape[1] + pad
    sheet.resize((W * a.scale, H * a.scale), Image.NEAREST).save(PREVIEW / "pieces_sheet.png")
    print(f"sheet: {PREVIEW / 'pieces_sheet.png'}")


if __name__ == "__main__":
    main()
