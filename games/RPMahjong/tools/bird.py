"""Cut the sparrow's animations out of its animation sheet: tools/art/bird.txt.

    python tools/bird.py [SHEET.gif]          (default build/assets/Bird.gif)

The sheet is an animated GIF of every animation playing at once in a grid,
each pixel drawn 5x5. For each animation used here this finds the bird's
box over all frames (one origin for the whole animation, so a hop keeps its
bounce), samples it at one point a pixel, keeps its distinct frames in the
order they play, and maps its colours onto the game's palette. The bird
faces left; the game mirrors it.

tools/art/bird.txt is what tools/assets.py packs.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SCALE = 5
# The sheet's colours -> the game's palette (names as in the RPGame library's rpgame/Palette.h).
COLOURS = {
    (0, 0, 0): "k", (49, 45, 44): "k",                   # outline, dark streaks
    (107, 77, 57): "b", (132, 101, 85): "b",             # browns -> WOOD
    (160, 128, 112): "p",                                # light brown -> SKIN
    (160, 151, 149): "s", (199, 183, 179): "s",          # greys -> SILVER
    (223, 212, 211): "w",                                # white
    (153, 102, 102): "m", (113, 64, 64): "m",            # feet -> WINE
    (179, 142, 72): "y",                                 # beak -> GOLD
}
# Where each animation plays on the sheet (a box round it, in sheet pixels),
# in the order Stage.cpp numbers them.
ANIMS = [
    ("fly", (420, 640, 560, 770)), ("takeoff", (700, 900, 560, 745)), ("idle", (150, 330, 100, 275)),
    ("idle2", (400, 600, 100, 275)), ("idle3", (680, 900, 100, 275)), ("hop", (560, 740, 330, 495)),
    ("walk", (320, 500, 330, 495)), ("peck", (160, 340, 600, 740)), ("eat", (790, 960, 380, 495)),
]


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "assets" / "Bird.gif"
    im = Image.open(src)
    keys = []
    for k in range(im.n_frames):
        im.seek(k)
        a = np.asarray(im.convert("RGB")).astype(np.int32)
        keys.append((a[..., 0] << 16) | (a[..., 1] << 8) | a[..., 2])
    lut = {(r << 16) | (g << 8) | b: ch for (r, g, b), ch in COLOURS.items()}
    bird = np.any([np.isin(k, list(lut)) for k in keys], axis=0)
    out = ["# The sparrow, cut from its animation sheet by python tools/bird.py: each",
           "# animation's frames in the order they play, facing left. '@ name w h'",
           "# starts an animation; its frames follow, a blank line apart. Letters as in",
           "# tools/art/*.txt (k ink, b wood, p skin, s silver, w white, m wine, y gold).", ""]
    total = 0
    for name, (x0, x1, y0, y1) in ANIMS:
        ys, xs = np.nonzero(bird[y0:y1, x0:x1])
        bx, by = xs.min() + x0, ys.min() + y0
        w, h = (xs.max() + x0 - bx + 1) // SCALE, (ys.max() + y0 - by + 1) // SCALE
        frames, order = [], []
        for k in keys:
            img = tuple("".join(lut.get(int(k[by + SCALE * j + 2, bx + SCALE * i + 2]), ".") for i in range(w))
                        for j in range(h))
            if img not in frames:
                frames.append(img)
            if not order or order[-1] != frames.index(img):
                order.append(frames.index(img))
        # One cycle of the order (the sheet loops each animation).
        cycle = order[:]
        for n in range(1, len(order)):
            if order[n:n + n] == order[:n] and len(set(order[:n])) == len(frames):
                cycle = order[:n]
                break
        out.append(f"@ {name} {w} {h}")
        out.append("# plays: " + " ".join(map(str, cycle)))
        for f in frames:
            out += list(f) + [""]
        total += len(frames)
        print(f"{name:8s} {w}x{h}, {len(frames)} frames, plays {cycle}")
    (HERE / "art" / "bird.txt").write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(f"tools/art/bird.txt: {total} frames")


if __name__ == "__main__":
    main()
