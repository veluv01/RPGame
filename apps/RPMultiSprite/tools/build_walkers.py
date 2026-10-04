"""Build the RPMultiSprite walker assets from SpriteSource/.

Moog and Mushboom were each quantised to their own 16-colour palette, but the
RPGfx framebuffer is 4 bpp with ONE palette. This tool gives both characters
a single shared palette and repacks every frame into one sheet file:

  sample/WALK.BIN      24 blocks of 512 bytes, one frame per block:
                         blocks  0..11  Moog      frames 0..11
                         blocks 12..23  Mushboom  frames 0..11
                       each block = [w, h, rows...] (Simon's sprite format)
                       zero-padded to 512. One contiguous file is what lets
                       the sketch fetch many frames with a single CMD18.
  src/WalkerData.h     the shared palette, sizes, and the same re-quantised
                       frames in PROGMEM (the sketch's FLASH baseline mode)
  sample/preview.png   original (top) vs re-quantised (bottom), 4x, per
                       character, plus the palette - to judge the colour shift

Palette: index 0 is the background (the blit treats 0 as transparent, so a
cleared framebuffer IS the background). Indices 1..15 are the sprite colours:
the 26 distinct source colours are merged down to 15 by weighted Ward
agglomeration in CIELAB. Weights are sqrt(pixel count), so small but
important details (eyes, highlights) are not the first to be merged away.

usage: python tools/build_walkers.py [--bg R,G,B] [--src DIR]
"""
import argparse, math, os, re, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.dirname(HERE)

# (zip, header, 8.3-safe name used in the generated code)
CHARACTERS = [
    ('MOOG.ZIP', 'Moog.h', 'Moog'),
    ('MUSHBOOM.ZIP', 'Mushboom.h', 'Mush'),
]
FRAMES_PER_CHAR = 12
BLOCK = 512
SPRITE_COLOURS = 15


# --------------------------------------------------------------------------
# Colour helpers
# --------------------------------------------------------------------------
def rgb565_to_rgb(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return ((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6)


def rgb_to_565(r, g, b):
    r, g, b = (max(0, min(255, int(round(v)))) for v in (r, g, b))
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb_to_lab(rgb):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v) for v in rgb)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = (0.2126 * r + 0.7152 * g + 0.0722 * b)
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    def f(t):
        return t ** (1 / 3) if t > 216 / 24389 else (24389 / 27 * t + 16) / 116
    fx, fy, fz = f(x), f(y), f(z)
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))


# --------------------------------------------------------------------------
# Input
# --------------------------------------------------------------------------
def parse_header(path):
    text = open(path, encoding='utf-8').read()
    m = re.search(r'uint16_t\s+\w+\s*\[\s*16\s*\]\s*=\s*\{([^}]*)\}', text)
    if not m:
        raise SystemExit(f'{path}: no 16-entry palette found')
    pal = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', m.group(1))]
    w = int(re.search(r'_X\s*=\s*(\d+)', text).group(1))
    h = int(re.search(r'_Y\s*=\s*(\d+)', text).group(1))
    return pal, w, h


def read_frames(zpath, w, h):
    """Return 12 frames, each a list of h rows of w palette indices."""
    rb = (w + 1) // 2
    with zipfile.ZipFile(zpath) as z:
        names = sorted(n for n in z.namelist() if n.upper().endswith('.BIN'))
        if len(names) != FRAMES_PER_CHAR:
            raise SystemExit(f'{zpath}: expected {FRAMES_PER_CHAR} BINs, found {len(names)}')
        frames = []
        for n in names:
            data = z.read(n)
            if data[0] != w or data[1] != h or len(data) < 2 + rb * h:
                raise SystemExit(f'{zpath}/{n}: header {data[0]}x{data[1]} != {w}x{h}')
            rows = []
            for y in range(h):
                row = data[2 + y * rb: 2 + (y + 1) * rb]
                px = []
                for x in range(w):
                    b = row[x >> 1]
                    px.append(b & 15 if not (x & 1) else b >> 4)
                rows.append(px)
            frames.append(rows)
    return frames


# --------------------------------------------------------------------------
# Shared palette: weighted Ward merging in Lab
# --------------------------------------------------------------------------
def build_palette(counts):
    """counts: {rgb565: pixels}. Returns (palette565[15], {rgb565: 1..15})."""
    clusters = []
    for c565, n in sorted(counts.items()):
        rgb = rgb565_to_rgb(c565)
        clusters.append({'w': math.sqrt(n), 'lab': rgb_to_lab(rgb), 'rgb': rgb,
                         'members': [c565]})
    while len(clusters) > SPRITE_COLOURS:
        best = None
        for i in range(len(clusters)):
            for j in range(i + 1, len(clusters)):
                a, b = clusters[i], clusters[j]
                d2 = sum((p - q) ** 2 for p, q in zip(a['lab'], b['lab']))
                cost = a['w'] * b['w'] / (a['w'] + b['w']) * d2
                if best is None or cost < best[0]:
                    best = (cost, i, j)
        _, i, j = best
        a, b = clusters[i], clusters[j]
        w = a['w'] + b['w']
        mix = lambda p, q: tuple((x * a['w'] + y * b['w']) / w for x, y in zip(p, q))
        merged = {'w': w, 'lab': mix(a['lab'], b['lab']), 'rgb': mix(a['rgb'], b['rgb']),
                  'members': a['members'] + b['members']}
        clusters[i] = merged
        del clusters[j]
    # Dark to light, so the palette reads sensibly in the header.
    clusters.sort(key=lambda c: c['lab'][0])
    palette = [rgb_to_565(*c['rgb']) for c in clusters]
    remap = {}
    for idx, c in enumerate(clusters):
        for m in c['members']:
            remap[m] = idx + 1
    return palette, remap


# --------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------
def pack(rows, w):
    out = bytearray()
    for px in rows:
        for x in range(0, w, 2):
            lo = px[x]
            hi = px[x + 1] if x + 1 < w else 0
            out.append(lo | (hi << 4))
    return bytes(out)


def write_header(path, palette, chars, packed):
    L = []
    L.append('/* Generated by tools/build_walkers.py - do not edit by hand. */')
    L.append('#pragma once')
    L.append('#include <stdint.h>')
    L.append('')
    L.append('/* Shared palette. 0 = background (also the transparent index of the')
    L.append(' * sprite data), 1..15 = sprite colours merged from both characters. */')
    L.append('static const uint16_t walkerPalette[16] = {')
    L.append('    ' + ', '.join(f'0x{c:04X}' for c in palette[:8]) + ',')
    L.append('    ' + ', '.join(f'0x{c:04X}' for c in palette[8:]))
    L.append('};')
    L.append('')
    L.append(f'#define WALK_TYPES           {len(chars)}')
    L.append(f'#define WALK_FRAMES_PER_TYPE {FRAMES_PER_CHAR}')
    L.append(f'#define WALK_FRAMES          {len(chars) * FRAMES_PER_CHAR}   /* = blocks in WALK.BIN */')
    L.append('')
    L.append('/* Per character type (Moog, Mush). Frame f of type t is sheet frame')
    L.append(' * t * WALK_FRAMES_PER_TYPE + f. Frames 0-2 walk up, 3-5 right,')
    L.append(' * 6-8 down, 9-11 left. */')
    L.append('static const uint8_t walkW[WALK_TYPES] = { ' + ', '.join(str(c['w']) for c in chars) + ' };')
    L.append('static const uint8_t walkH[WALK_TYPES] = { ' + ', '.join(str(c['h']) for c in chars) + ' };')
    L.append('')
    L.append('/* The same pixels as WALK.BIN (rows only, no header), for the FLASH')
    L.append(' * baseline mode. Packing is the framebuffer\'s: even x in the low nibble. */')
    names = []
    for t, c in enumerate(chars):
        rb = (c['w'] + 1) // 2
        for f in range(FRAMES_PER_CHAR):
            name = f'walk{c["name"]}_{f:02d}'
            names.append(name)
            data = packed[t * FRAMES_PER_CHAR + f][2:]
            L.append(f'static const uint8_t {name}[] PROGMEM = {{')
            for y in range(c['h']):
                row = data[y * rb:(y + 1) * rb]
                L.append('    ' + ', '.join(f'0x{b:02X}' for b in row) + ',')
            L.append('};')
    L.append('')
    L.append('static const uint8_t *const walkFlashFrames[WALK_FRAMES] = {')
    for i in range(0, len(names), 4):
        L.append('    ' + ', '.join(names[i:i + 4]) + ',')
    L.append('};')
    L.append('')
    open(path, 'w', newline='\n').write('\n'.join(L))


def write_preview(path, bg, palette, chars, remap, scale=4):
    try:
        from PIL import Image
    except ImportError:
        print('Pillow not installed - skipping preview.png')
        return
    pad = 2
    maxw = max(c['w'] for c in chars)
    maxh = max(c['h'] for c in chars)
    cols = FRAMES_PER_CHAR
    W = cols * (maxw + pad) + pad
    H = len(chars) * 2 * (maxh + pad) + pad + 10
    img = Image.new('RGB', (W, H), bg)
    shared = [bg] + [rgb565_to_rgb(c) for c in palette]
    y0 = pad
    for c in chars:
        src = [rgb565_to_rgb(v) for v in c['pal']]
        for variant in (0, 1):
            for f, rows in enumerate(c['frames']):
                x0 = pad + f * (maxw + pad)
                for y, px in enumerate(rows):
                    for x, i in enumerate(px):
                        if i == 0:
                            continue
                        col = src[i] if variant == 0 else shared[remap[c['pal'][i]]]
                        img.putpixel((x0 + x, y0 + y), col)
            y0 += maxh + pad
    # Palette swatches along the bottom.
    sw = W // 16
    for i, col in enumerate(shared):
        for y in range(H - 9, H - 1):
            for x in range(i * sw, (i + 1) * sw - 1):
                img.putpixel((x, y), col)
    img = img.resize((W * scale, H * scale), Image.NEAREST)
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=os.path.join(os.path.dirname(SKETCH), 'SpriteSource'))
    ap.add_argument('--bg', default='48,88,56', help='background colour R,G,B (palette index 0)')
    args = ap.parse_args()
    bg = tuple(int(v) for v in args.bg.split(','))

    chars = []
    counts = {}
    for zname, hname, name in CHARACTERS:
        pal, w, h = parse_header(os.path.join(args.src, hname))
        frames = read_frames(os.path.join(args.src, zname), w, h)
        if (2 + (w + 1) // 2 * h) > BLOCK:
            raise SystemExit(f'{name}: a {w}x{h} frame does not fit in one 512-byte block')
        for rows in frames:
            for px in rows:
                for i in px:
                    if i:
                        counts[pal[i]] = counts.get(pal[i], 0) + 1
        chars.append({'name': name, 'pal': pal, 'w': w, 'h': h, 'frames': frames})

    spritePal, remap = build_palette(counts)
    palette = [rgb_to_565(*bg)] + spritePal

    # Remap and pack every frame into its own block.
    packed = []
    sheet = bytearray()
    err = 0.0
    npx = 0
    for c in chars:
        for rows in c['frames']:
            out = [[remap[c['pal'][i]] if i else 0 for i in px] for px in rows]
            for px in rows:
                for i in px:
                    if i:
                        a = rgb_to_lab(rgb565_to_rgb(c['pal'][i]))
                        b = rgb_to_lab(rgb565_to_rgb(palette[remap[c['pal'][i]]]))
                        err += math.dist(a, b)
                        npx += 1
            blob = bytes([c['w'], c['h']]) + pack(out, c['w'])
            packed.append(blob)
            sheet += blob + bytes(BLOCK - len(blob))

    os.makedirs(os.path.join(SKETCH, 'sample'), exist_ok=True)
    binPath = os.path.join(SKETCH, 'sample', 'WALK.BIN')
    open(binPath, 'wb').write(sheet)
    write_header(os.path.join(SKETCH, 'src', 'WalkerData.h'), palette, chars, packed)
    write_preview(os.path.join(SKETCH, 'sample', 'preview.png'), bg, spritePal, chars, remap)

    print(f'{len(counts)} source colours -> {SPRITE_COLOURS} shared (+ background)')
    print(f'mean colour error {err / npx:.2f} dE76 over {npx} px')
    print('palette: ' + ' '.join(f'{c:04X}' for c in palette))
    print(f'wrote {binPath} ({len(sheet)} bytes, {len(packed)} frames)')


if __name__ == '__main__':
    main()
