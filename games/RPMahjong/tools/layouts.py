"""Turn the layout maps in tools/layouts/*.txt into src/game/Layouts.{h,cpp}.

    python tools/layouts.py

A map is a text grid per layer, one character for half a tile each way
(30 x 16), with an X at every tile's top-left corner; files are taken in
name order. Each layout is checked before it is written:

  * an even number of tiles, 144 at most, none overlapping;
  * every tile above the table rests on tiles under all four of its corners;
  * it fits the screen (Stage.cpp's geometry, mirrored below);
  * a deal can be found for it (the game's own method: take a full table
    apart two free tiles at a time), and how often that method runs into a
    dead end;
  * no more free tiles at once than the game's lists hold.

Also writes a picture of each to build/layouts/.
"""
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = ROOT / "src" / "game"
MAX_TILES, MAX_FREE, LAYERS = 144, 64, 5
W2, H2 = 30, 16
# Stage geometry: a tile is 8x12, layer z is drawn 2z px up and left.
OX, OY, TOP, BOTTOM = 4, 14, 12, 115


def parse(path):
    name, tiles, z = None, [], None
    y = 0
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line:
            continue
        if line.startswith("name "):
            name = line[5:].strip()
        elif line.startswith("layer "):
            z, y = int(line[6:]), 0
        else:
            if z is None:
                raise SystemExit(f"{path.name}: grid before 'layer'")
            if y >= H2 or len(line) > W2:
                raise SystemExit(f"{path.name}: layer {z} is bigger than {W2} x {H2}")
            tiles += [(x, y, z) for x, ch in enumerate(line) if ch == "X"]
            y += 1
    if not name:
        raise SystemExit(f"{path.name}: no name")
    return name, tiles


def blockers(tiles):
    """For each tile: (tiles on it, tiles at its left, tiles at its right)."""
    at = {t: i for i, t in enumerate(tiles)}
    res = []
    for (x, y, z) in tiles:
        over = [at[(x + dx, y + dy, z + 1)] for dx in (-1, 0, 1) for dy in (-1, 0, 1) if (x + dx, y + dy, z + 1) in at]
        left = [at[(x - 2, y + dy, z)] for dy in (-1, 0, 1) if (x - 2, y + dy, z) in at]
        right = [at[(x + 2, y + dy, z)] for dy in (-1, 0, 1) if (x + 2, y + dy, z) in at]
        res.append((over, left, right))
    return res


def free_tiles(here, blk):
    out = []
    for i in here:
        over, left, right = blk[i]
        if any(j in here for j in over):
            continue
        if any(j in here for j in left) and any(j in here for j in right):
            continue
        out.append(i)
    return out


def check(path, name, tiles):
    def fail(msg):
        raise SystemExit(f"{path.name}: {msg}")
    n = len(tiles)
    if n % 2 or n > MAX_TILES or n < 2:
        fail(f"{n} tiles (need an even number, at most {MAX_TILES})")
    s = set(tiles)
    if len(s) != n:
        fail("a tile is listed twice")
    for (x, y, z) in tiles:
        if not (0 <= x <= W2 - 2 and 0 <= y <= H2 - 2 and 0 <= z < LAYERS):
            fail(f"tile at {x},{y} layer {z} is off the grid")
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if (dx or dy) and (x + dx, y + dy, z) in s:
                    fail(f"tiles overlap at {x},{y} layer {z}")
        if z:
            for cx in (x, x + 1):
                for cy in (y, y + 1):
                    if not any((cx - ax, cy - ay, z - 1) in s for ax in (0, 1) for ay in (0, 1)):
                        fail(f"tile at {x},{y} layer {z} hangs over nothing")
        sx, sy = OX + 4 * x - 2 * z, OY + 6 * y - 2 * z
        if sx < 0 or sx + 10 > 128 or sy < TOP or sy + 14 > BOTTOM + 1:
            fail(f"tile at {x},{y} layer {z} is off the screen ({sx},{sy})")
    blk = blockers(tiles)
    rng = random.Random(1)
    dead, most = 0, 0
    runs = 2000
    for _ in range(runs):
        here = set(range(n))
        while here:
            f = free_tiles(here, blk)
            most = max(most, len(f))
            if len(f) < 2:
                dead += 1
                break
            a, b = rng.sample(f, 2)
            here -= {a, b}
    if dead == runs:
        fail("no deal found: it cannot be cleared")
    if most > MAX_FREE:
        fail(f"{most} tiles free at once (the game's lists hold {MAX_FREE})")
    return dead / runs, most


def runs_of(tiles):
    """(z, y2, x2, n): n tiles two apart along a row."""
    out = []
    for (x, y, z) in sorted(tiles, key=lambda t: (t[2], t[1], t[0])):
        if out and out[-1][0] == z and out[-1][1] == y and out[-1][2] + 2 * out[-1][3] == x:
            out[-1][3] += 1
        else:
            out.append([z, y, x, 1])
    return out


def picture(name, tiles):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    s = 4
    im = Image.new("RGB", (128 * s, 128 * s), (17, 119, 51))
    d = ImageDraw.Draw(im)
    shade = [(150, 150, 160), (185, 185, 195), (215, 215, 225), (238, 238, 245), (255, 255, 255)]
    for (x, y, z) in sorted(tiles, key=lambda t: (t[2], t[0] + t[1], t[0])):
        sx, sy = OX + 4 * x - 2 * z, OY + 6 * y - 2 * z
        d.rectangle([(sx + 2) * s, (sy + 2) * s, (sx + 10) * s - 1, (sy + 14) * s - 1], fill=(60, 40, 10))
        d.rectangle([sx * s, sy * s, (sx + 8) * s - 1, (sy + 12) * s - 1], fill=shade[z], outline=(119, 68, 17))
    out = ROOT / "build" / "layouts"
    out.mkdir(parents=True, exist_ok=True)
    im.save(out / f"{name.lower()}.png")


def main():
    files = sorted((HERE / "layouts").glob("*.txt"))
    layouts = []
    for f in files:
        name, tiles = parse(f)
        dead, most = check(f, name, tiles)
        layouts.append((name, tiles))
        picture(name, tiles)
        print(f"{name:8s} {len(tiles):3d} tiles, {len(runs_of(tiles)):2d} runs, "
              f"dead ends {100 * dead:4.1f}%, most free {most}")
    h = ["// Generated by tools/layouts.py from tools/layouts/*.txt - do not edit.",
         "#pragma once", "#include <stdint.h>", "", "namespace board {", "",
         "// Per layout: runs of (layer, y2, x2, tiles two apart), ended by 0xFF.",
         f"extern const uint8_t *const LAYOUT[{len(layouts)}];",
         f"extern const char *const LAYOUT_NAME[{len(layouts)}];",
         f"extern const uint8_t LAYOUT_TILES[{len(layouts)}];", "", "}  // namespace board", ""]
    c = ["// Generated by tools/layouts.py from tools/layouts/*.txt - do not edit.",
         '#include "Layouts.h"', "", "namespace board {", ""]
    for i, (name, tiles) in enumerate(layouts):
        c.append(f"static const uint8_t L{i}[] = {{   // {name}")
        for r in runs_of(tiles):
            c.append("    " + ", ".join(str(v) for v in r) + ",")
        c.append("    0xFF,")
        c.append("};")
    c.append("")
    c.append(f"const uint8_t *const LAYOUT[{len(layouts)}] = {{" + ", ".join(f"L{i}" for i in range(len(layouts))) + "};")
    c.append(f"const char *const LAYOUT_NAME[{len(layouts)}] = {{" + ", ".join(f'"{n}"' for n, _ in layouts) + "};")
    c.append(f"const uint8_t LAYOUT_TILES[{len(layouts)}] = {{" + ", ".join(str(len(t)) for _, t in layouts) + "};")
    c += ["", "}  // namespace board", ""]
    (OUT / "Layouts.h").write_text("\n".join(h), encoding="utf-8", newline="\n")
    (OUT / "Layouts.cpp").write_text("\n".join(c), encoding="utf-8", newline="\n")
    if len(layouts) != 4:
        print(f"note: board::LAYOUTS in MahjongBoard.h must be {len(layouts)}", file=sys.stderr)


if __name__ == "__main__":
    main()
