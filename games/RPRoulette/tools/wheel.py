#!/usr/bin/env python3
"""wheel: the roulette wheel's angle map generator and reference renderer.

Implements docs/design/wheel.md sections 0-5:
  - the geometry of the stacked bowl layers and the rotor rings (section 1);
  - the one-quadrant polar angle map, built with RPGfx's EllipseRows
    arithmetic and centred on a pixel centre (sections 2.1, 2.3), and its C
    tables (write_c -> src/assets/WheelMap.{h,cpp});
  - the per-frame 1 KB colour LUT (2.4) and the ring loop (2.5), mirrored
    here so the previews show what the device will draw. ring() reads the
    map and the LUT only - no trig per pixel - and ring_packed() runs the
    spec's nibble-packed C loop line for line as a cross-check;
  - the static layers, deflectors, cone, turret and the ball projection
    (1.2, 3.4, 4), with the depth order of section 4.

    python tools/wheel.py              # C tables + previews in out/wheel/
    python tools/wheel.py --bins 512   # write the 512-step map into WheelMap.* instead
    python tools/wheel.py --no-png     # C tables only

Deviations from the spec (all drawing only; the map, LUT and ring are as
written): deflectors are SILVER and drawn after the ring, near ones 1 row
lower; the ball has a 1 px INK rim; the turret arms have an INK shadow line
and the dome a WINE edge. Each is explained where it is drawn.
"""
import argparse
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))   # the repository's tools/: pixkit
from pixkit import (FB, ellipse_rows, isin, INK, WHITE, FELT_DK, FELT,  # noqa: E402
                    SILVER, RED, WINE, GOLD, WOOD, NAVY, FX_A, FX_B)

GAME = HERE.parent
ASSETS = GAME / "src" / "assets"
OUT = GAME / "out" / "wheel"

# ---------------------------------------------------------------------------
# Wheel orders and pocket colours (section 1.3)
# ---------------------------------------------------------------------------
# Clockwise as seen from above (screen angle grows clockwise, y down), so
# wheel index p sits at angle p/n of a turn from the rotor's zero mark.
N00 = 37                                           # the number 00
EU_ORDER = [0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23,
            10, 5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26]
US_ORDER = [0, 28, 9, 26, 30, 11, 7, 20, 32, 17, 5, 22, 34, 15, 3, 24, 36, 13, 1,
            N00, 27, 10, 25, 29, 12, 8, 19, 31, 18, 6, 21, 33, 16, 4, 23, 35, 14, 2]
RED_NUMBERS = frozenset((1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27,
                         30, 32, 34, 36))
GREEN, REDK, BLACK = 0, 1, 2                       # colour classes: RING_C/FLOOR_C index


def number_name(num):
    return "00" if num == N00 else str(num)


def number_class(num):
    """The real colour of a number."""
    if num in (0, N00):
        return GREEN
    return REDK if num in RED_NUMBERS else BLACK


def pocket_class(idx, n):
    """The device's parity rule: no colour table needed."""
    if n == 37:
        return GREEN if idx == 0 else (REDK if idx & 1 else BLACK)
    return GREEN if idx in (0, 19) else (BLACK if idx & 1 else REDK)


def classes(n):
    return [pocket_class(i, n) for i in range(n)]


def wheel_order(n):
    return EU_ORDER if n == 37 else US_ORDER


def verify_orders():
    """The parity rule against the real number colours, plus sanity checks."""
    assert len(RED_NUMBERS) == 18
    for n, order in ((37, EU_ORDER), (38, US_ORDER)):
        assert len(order) == n and sorted(order) == list(range(n)), n
        for i, num in enumerate(order):
            assert pocket_class(i, n) == number_class(num), (n, i, num)
        for i in range(n):                         # red and black alternate
            a, b = number_class(order[i]), number_class(order[(i + 1) % n])
            assert GREEN in (a, b) or a != b, (n, i)
    assert (EU_ORDER[-1], EU_ORDER[1]) == (26, 32)   # EU 0 lies between 26 and 32
    assert US_ORDER[19] == N00                         # US 00 is opposite the 0
    assert (US_ORDER[18], US_ORDER[20]) == (1, 27)


# ---------------------------------------------------------------------------
# Geometry (section 1)
# ---------------------------------------------------------------------------
CX, CY = 64, 86               # wheel centre; cx must be even (pixel pairs)
BAND_Y0, BAND_Y1 = 46, 128    # the table band
WALL_H = 46

# Bowl layers, outermost first: (name, dx, dy, R, fill, outline). ry = R/2.
LAYERS = [
    ("shadow",      2,  5, 60, FELT_DK, None),
    ("side face",   0,  3, 60, WINE,    INK),
    ("rim top",     0, -4, 60, WOOD,    GOLD),
    ("bowl wall",   0, -4, 56, INK,     None),
    ("ball track",  0, -3, 54, NAVY,    None),
    ("apron",       0, -2, 50, WOOD,    None),   # the deflector ring
    ("rotor gap",   0, -1, 46, INK,     None),
]
R_OUT, R_IN = 44, 26          # the rotor, drawn from the angle map
R_LIP = 42                    # 44..42 GOLD lip                     band 3
R_MARK = (40, 38)             # 40..38 WHITE marks (inside 42..36)  band 2
R_NUM = 36                    # 42..36 number ring                  band 1
R_SEP = 34                    # 36..34 GOLD separator               band 3
#                               34..26 pocket floors                band 0
R_TRACK, R_DEFL, R_REST = 52, 48, 30
DEFL_DY = -2                  # deflectors sit on the apron's centre
CONE_R, INLAY_R, INLAY_DY = 26, 18, -1

_ROWS = {}


def ell(R):
    """Row half-widths of the R x R/2 ellipse (RPGfx EllipseRows)."""
    if R not in _ROWS:
        _ROWS[R] = ellipse_rows(R, R // 2)
    return _ROWS[R]


def inside(R, i, j):
    """Pixel (i, j) from the centre is inside gfx_fillEllipse(R, R/2)."""
    return j <= R // 2 and i <= ell(R)[j]


def band_of(i, j):
    if not inside(R_LIP, i, j):
        return 3
    if inside(R_MARK[0], i, j) and not inside(R_MARK[1], i, j):
        return 2
    if not inside(R_NUM, i, j):
        return 1
    if not inside(R_SEP, i, j):
        return 3
    return 0


# ---------------------------------------------------------------------------
# The angle map (section 2.1)
# ---------------------------------------------------------------------------
def quadrant_q(i, j, bins=256):
    """floor(atan2(2j, i) in steps of 1/bins turn), clamped to the quadrant.

    The pixel (i, j) is measured from the centre pixel's centre, and the
    2j undoes the 2:1 squash. Only tan = 0, 1 and inf are rational, so the
    diagonal is the one interior pixel that can sit exactly on a step
    boundary; it is handled exactly instead of trusting the float.
    """
    qn = bins // 4
    if i == 2 * j:
        return qn // 2
    t = math.atan2(2 * j, i) * (bins / 2) / math.pi
    q = math.floor(t)
    assert i == 0 or j == 0 or abs(t - round(t)) > 1e-9, (i, j)
    return min(qn - 1, q)


def build_map(bins=256):
    """(spans, map bytes) for one quadrant, rows j = 0..R_OUT/2 below centre.

    256 bins: byte = band << 6 | q (q 0..63).
    512 bins: byte = number_ring << 7 | q (q 0..127); the GOLD rings and the
    WHITE marks are not in the map then (section 2.2).
    """
    spans, data = [], bytearray()
    for j in range(R_OUT // 2 + 1):
        a = ell(R_IN)[j] + 1 if j <= R_IN // 2 else 0
        b = ell(R_OUT)[j] + 1
        spans.append((a, b))
        for i in range(a, b):
            assert inside(R_OUT, i, j) and not inside(R_IN, i, j)
            q = quadrant_q(i, j, bins)
            if bins == 256:
                data.append(band_of(i, j) << 6 | q)
            else:
                data.append((0 if inside(R_NUM, i, j) else 1) << 7 | q)
    # Every ring pixel of the quadrant is in exactly one span.
    assert sum(b - a for a, b in spans) == len(data)
    return spans, bytes(data)


def full_step(q, right, down, bins=256):
    """The full-turn step of a quadrant byte's q (the mirroring of 2.1)."""
    h = bins // 2
    if down:
        return q if right else h - 1 - q
    return h + q if not right else bins - 1 - q


# ---------------------------------------------------------------------------
# C tables
# ---------------------------------------------------------------------------
def _c_bytes(data, per_line=16, indent="    "):
    lines = []
    for k in range(0, len(data), per_line):
        lines.append(indent + ", ".join(f"{v:3d}" for v in data[k:k + per_line]) + ",")
    return "\n".join(lines)


def write_c(path_h, path_cpp, bins=256):
    spans, data = build_map(bins)
    rows = len(spans)
    span_bytes = 2 * rows
    path_h, path_cpp = Path(path_h), Path(path_cpp)
    path_h.parent.mkdir(parents=True, exist_ok=True)
    fmt = ("band<<6 | q: band 0 floor, 1 number ring, 2 mark, 3 GOLD; q 0..63"
           if bins == 256 else
           "ring<<7 | q: ring 0 floor, 1 number ring; q 0..127")
    a_max_row = max(j for j, (a, _) in enumerate(spans) if a)
    h = f"""// Generated by tools/wheel.py - do not edit.
// The roulette rotor's polar angle map (docs/design/wheel.md section 2.1):
// one quadrant around the pixel centre (cx, cy), rows j = 0..{rows - 1} below it.
// Mirroring: right half i >= 0, left half i >= 1; rows below j >= 0, above j >= 1.
// Full-turn step s from q: right-down q, left-down {bins // 2 - 1}-q, left-up {bins // 2}+q,
// right-up {bins - 1}-q ({bins} steps per turn, floor-based, clockwise from +x).
#pragma once
#include <stdint.h>

constexpr int WHEEL_ROWS = {rows}, WHEEL_R_IN = {R_IN}, WHEEL_R_OUT = {R_OUT};
constexpr int WHEEL_BINS = {bins};              // angle steps per turn
constexpr int WHEEL_MAP_SIZE = {len(data)};
extern const uint8_t WHEEL_SPAN[WHEEL_ROWS][2];   // row j: pixels dx = i for i in [a, b); a = 0 once j > {a_max_row}
extern const uint8_t WHEEL_MAP[WHEEL_MAP_SIZE];   // rows packed together, 1 byte per pixel: {fmt}

// Wheel orders: wheel index -> number, clockwise from the zero. 37 means 00.
constexpr uint8_t WHEEL_00 = {N00};
extern const uint8_t EU_ORDER[37];
extern const uint8_t US_ORDER[38];
"""
    def macro(name, body):
        return f"#define {name} {{ \\\n" + " \\\n".join(body.split("\n")) + " \\\n}"

    span_txt = "\n".join("    {%2d, %2d}," % s for s in spans)
    cpp = f"""// Generated by tools/wheel.py - do not edit.
#include "WheelMap.h"

// Each table is written once, as a macro, so that a compile-time copy can
// check its length (GCC keeps a declared bound and zero-fills a short
// initializer silently) and the spans can be summed.
{macro("WHEEL_SPAN_INIT", span_txt)}

{macro("WHEEL_MAP_INIT", _c_bytes(data))}

{macro("EU_ORDER_INIT", _c_bytes(EU_ORDER, 19))}

{macro("US_ORDER_INIT", _c_bytes(US_ORDER, 19))}

const uint8_t WHEEL_SPAN[WHEEL_ROWS][2] = WHEEL_SPAN_INIT;
const uint8_t WHEEL_MAP[WHEEL_MAP_SIZE] = WHEEL_MAP_INIT;
const uint8_t EU_ORDER[37] = EU_ORDER_INIT;
const uint8_t US_ORDER[38] = US_ORDER_INIT;

namespace {{
constexpr uint8_t kSpan[][2] = WHEEL_SPAN_INIT;   // constant-evaluated only: no storage
constexpr uint8_t kMap[] = WHEEL_MAP_INIT;
constexpr uint8_t kEu[] = EU_ORDER_INIT;
constexpr uint8_t kUs[] = US_ORDER_INIT;
constexpr int spanSum(int j) {{ return j < WHEEL_ROWS ? kSpan[j][1] - kSpan[j][0] + spanSum(j + 1) : 0; }}
}}  // namespace

static_assert(sizeof(kSpan) == {span_bytes}, "WHEEL_SPAN: one pair per row");
static_assert(sizeof(kMap) == WHEEL_MAP_SIZE, "WHEEL_MAP size");
static_assert(spanSum(0) == WHEEL_MAP_SIZE, "the spans cover the map exactly");
static_assert(sizeof(kEu) == 37 && sizeof(kUs) == 38, "wheel orders");
"""
    path_h.write_text(h, encoding="utf-8", newline="\n")
    path_cpp.write_text(cpp, encoding="utf-8", newline="\n")
    return len(data), span_bytes


# ---------------------------------------------------------------------------
# Per-frame LUT (section 2.4) and the ring loop (2.5)
# ---------------------------------------------------------------------------
RING_C = (FELT, RED, INK)
FLOOR_C = (FELT_DK, WINE, INK)


def build_lut(rho, n, cls, hi_p=None, hi_c=None):
    """The 1,024 B LUT: 4 quadrant blocks (RD, LD, LU, RU) x 4 bands x 64.

    rho is the rotor angle in pocket units (1 turn = n << 16). Colours are
    sampled at step centres, so frets are always exactly 2 steps wide.
    """
    T, step = n << 16, n << 8
    FR, DV, MK = n << 8, n << 7, n << 7            # half-widths: fret 2, divider 1, mark 1 step
    hi_p = 0xFF if hi_p is None else hi_p
    lut = bytearray(1024)
    u = ((n << 7) + T - rho % T) % T               # centre of step 0, wheel frame
    for s in range(256):
        p, f = u >> 16, u & 0xFFFF
        k = cls[p]
        ring_c, flo = RING_C[k], FLOOR_C[k]
        if p == hi_p:
            ring_c = flo = hi_c
        c0 = SILVER if (f < FR or f >= 0x10000 - FR) else flo
        c1 = GOLD if (f < DV or f >= 0x10000 - DV) else ring_c
        c2 = WHITE if ((f - (0x8000 - MK)) & 0xFFFFFFFF) < 2 * MK else c1
        q = s & 63
        if s & 64:
            q = 63 - q                             # LD: 127-s, RU: 255-s
        d = (s >> 6) << 8
        lut[d + q], lut[d + 64 + q], lut[d + 128 + q], lut[d + 192 + q] = c0, c1, c2, GOLD
        u += step
        if u >= T:
            u -= T
    return lut


def build_lut512(rho, n, cls, hi_p=None, hi_c=None):
    """The --bins 512 LUT: 4 blocks x 2 rings x 128 (no marks, no GOLD band)."""
    T, step = n << 16, n << 7
    FR, DV = n << 7, n << 6                        # fret 2 steps, divider 1 step (of 512)
    hi_p = 0xFF if hi_p is None else hi_p
    lut = bytearray(1024)
    u = ((n << 6) + T - rho % T) % T
    for s in range(512):
        p, f = u >> 16, u & 0xFFFF
        k = cls[p]
        ring_c, flo = RING_C[k], FLOOR_C[k]
        if p == hi_p:
            ring_c = flo = hi_c
        c0 = SILVER if (f < FR or f >= 0x10000 - FR) else flo
        c1 = GOLD if (f < DV or f >= 0x10000 - DV) else ring_c
        q = s & 127
        if s & 128:
            q = 127 - q
        d = (s >> 7) << 8
        lut[d + q], lut[d + 128 + q] = c0, c1
        u += step
        if u >= T:
            u -= T
    return lut


MAP_CACHE = {}


def wheel_map(bins=256):
    if bins not in MAP_CACHE:
        MAP_CACHE[bins] = build_map(bins)
    return MAP_CACHE[bins]


def ring(fb, cx, cy, lut, x0=0, x1=128, y0=BAND_Y0, y1=BAND_Y1, bins=256):
    """The ring loop, pixel by pixel: map byte -> quadrant LUT -> colour.

    Writes fb directly in [x0, x1) x [y0, y1), as the device's RAMFUNC does
    (it ignores the gfx clip).
    """
    spans, m = wheel_map(bins)
    pos = 0
    for j, (a, b) in enumerate(spans):
        row = m[pos:pos + b - a]
        pos += b - a
        for up in (0, 1):
            if up and not j:
                continue
            y = cy - j if up else cy + j
            if y < y0 or y >= y1:
                continue
            base = y * 128
            L = (3 if up else 0) * 256             # right: RU / RD
            for i in range(max(a, x0 - cx), min(b, x1 - cx)):
                fb.p[base + cx + i] = lut[L + row[i - a]]
            L = (2 if up else 1) * 256             # left: LU / LD
            for i in range(max(a or 1, cx - x1 + 1), min(b, cx - x0 + 1)):
                fb.p[base + cx - i] = lut[L + row[i - a]]


def ring_packed(buf, cx, cy, lut, x0, x1, y0, y1):
    """Section 2.5's C loop, line for line, on a 4 bpp buffer (64 B rows,
    even x = low nibble). Only used to check the spec's nibble pairing."""
    spans, m = wheel_map(256)
    pos = 0
    for j, (a, b) in enumerate(spans):
        rm = pos - a                               # m[rm + i], i in [a, b)
        pos += b - a
        for up in (0, 1):
            if up and not j:
                continue
            y = cy - j if up else cy + j
            if y < y0 or y >= y1:
                continue
            fb = y * 64
            L = (3 if up else 0) * 256
            i = a if a > x0 - cx else x0 - cx
            e = b if b < x1 - cx else x1 - cx
            if i < e:
                p = fb + ((cx + i) >> 1)
                if i & 1:
                    buf[p] = (buf[p] & 0x0F) | (lut[L + m[rm + i]] << 4)
                    p += 1
                    i += 1
                while i + 1 < e:
                    buf[p] = lut[L + m[rm + i]] | (lut[L + m[rm + i + 1]] << 4)
                    p += 1
                    i += 2
                if i < e:
                    buf[p] = (buf[p] & 0xF0) | lut[L + m[rm + i]]
            L = (2 if up else 1) * 256
            i = a if a else 1
            if i < cx - x1 + 1:
                i = cx - x1 + 1
            e = b if b < cx - x0 + 1 else cx - x0 + 1
            if i < e:
                p = fb + ((cx - i) >> 1)
                if not i & 1:
                    buf[p] = (buf[p] & 0xF0) | lut[L + m[rm + i]]
                    p -= 1
                    i += 1
                while i + 1 < e:
                    buf[p] = lut[L + m[rm + i + 1]] | (lut[L + m[rm + i]] << 4)
                    p -= 1
                    i += 2
                if i < e:
                    buf[p] = (buf[p] & 0x0F) | (lut[L + m[rm + i]] << 4)


# ---------------------------------------------------------------------------
# Static layers, cone, turret (sections 1.2 and 4)
# ---------------------------------------------------------------------------
def draw_felt(fb):
    """BJ's felt: FELT, dithered FELT_DK edges, FELT_DK line at the rail."""
    fb.fill_rect(0, BAND_Y0, 128, BAND_Y1 - BAND_Y0, FELT)
    fb.dither(0, BAND_Y0, 3, BAND_Y1 - BAND_Y0, FELT_DK, 0)
    fb.dither(125, BAND_Y0, 3, BAND_Y1 - BAND_Y0, FELT_DK, 1)
    fb.hline(0, BAND_Y0, 128, FELT_DK)


def draw_bowl(fb, cx, cy):
    for _, dx, dy, R, fill, edge in LAYERS:
        fb.fill_ellipse(cx + dx, cy + dy, R, R // 2, fill)
        if edge is not None:
            fb.ellipse(cx + dx, cy + dy, R, R // 2, edge)


def deflector_pos(cx, cy, k):
    """Deflector k (0..7) at (2k+1)/16 of a turn, R48 on the apron.

    On the near half the rotor is painted over the apron down to its last
    row, so a near deflector moves down 1 row onto that visible strip
    (otherwise it sits on the rotor lip). The ball's crossing test is by
    angle, so this is drawing only.
    """
    a = (2 * k + 1) * 16
    s = isin(a)
    return (cx + ((R_DEFL * isin(a + 64) + 128) >> 8),
            cy + DEFL_DY + (((R_DEFL // 2) * s + 128) >> 8) + (1 if s > 0 else 0))


def draw_deflector(fb, cx, cy, k, flash=False):
    """SILVER with a WHITE centre (chrome): GOLD ones were invisible on the
    GOLD lip and faint on the WOOD apron. All WHITE while flashing."""
    x, y = deflector_pos(cx, cy, k)
    c = WHITE if flash else SILVER
    fb.hline(x - 1, y, 3, c)
    if not k & 1:                                  # plus / bar alternate
        fb.pixel(x, y - 1, c)
        fb.pixel(x, y + 1, c)
    fb.pixel(x, y, WHITE)


def draw_deflectors(fb, cx, cy, flash=()):
    for k in range(8):
        draw_deflector(fb, cx, cy, k, k in flash)


def draw_cone(fb, cx, cy):
    fb.fill_ellipse(cx, cy, CONE_R, CONE_R // 2, WOOD)
    fb.ellipse(cx, cy, CONE_R, CONE_R // 2, GOLD)            # the inner lip
    fb.ellipse(cx, cy + INLAY_DY, INLAY_R, INLAY_R // 2, WINE)


ARM_LEN = 12


def arm_tips(cx, cy, rotor_a8):
    hub_y = cy - 9
    tips = []
    for k in range(4):
        a = rotor_a8 + 64 * k
        c, s = isin(a + 64), isin(a)
        tips.append((cx + ((ARM_LEN * c + 128) >> 8),
                     hub_y + (((ARM_LEN // 2) * s + 128) >> 8), c, s))
    return tips


def draw_turret(fb, cx, cy, rotor_a8):
    """Dome, column, 4 arms turning with the rotor, cap and finial."""
    fb.fill_ellipse(cx, cy - 2, 8, 4, GOLD)
    fb.ellipse(cx, cy - 2, 8, 4, WINE)                       # WOOD is lost on the cone
    fb.fill_rect(cx - 2, cy - 9, 5, 7, GOLD)                 # x 62..66, y CY-9..CY-3
    fb.vline(cx - 1, cy - 9, 7, FX_B)                        # highlight
    fb.vline(cx + 2, cy - 9, 7, WOOD)                        # shade
    hub_y = cy - 9
    tips = arm_tips(cx, cy, rotor_a8)
    # Far arms first, so a near arm crossing one stays on top.
    for ex, ey, c, s in sorted(tips, key=lambda t: t[3]):
        fb.line(cx, hub_y + 1, ex, ey + 1, INK)              # shadow: lifts it off GOLD
        fb.line(cx, hub_y, ex, ey, GOLD)
        kx = ex if c >= 0 else ex - 1                        # knob grows outward
        ky = ey if s >= 0 else ey - 1
        fb.fill_rect(kx, ky, 2, 2, SILVER)
    fb.fill_ellipse(cx, hub_y, 3, 1, FX_B)                   # cap
    fb.pixel(cx, cy - 12, WHITE)                             # finial
    fb.pixel(cx, cy - 11, SILVER)


# ---------------------------------------------------------------------------
# Ball (section 3.4)
# ---------------------------------------------------------------------------
_ = None
# The 4x4 ball (transparent corners, WHITE body, SILVER lower right) inside
# a 1 px INK rim: without the rim a WHITE/SILVER ball vanishes among the
# WHITE marks and SILVER frets. Top-left at (x-3, y-3): the ball's centre
# is (x-0.5, y-0.5), the INK rim's lowest row is y+2.
BALL = [
    [_, _, INK, INK, _, _],
    [_, INK, WHITE, WHITE, INK, _],
    [INK, WHITE, WHITE, WHITE, SILVER, INK],
    [INK, WHITE, WHITE, SILVER, SILVER, INK],
    [_, INK, SILVER, SILVER, INK, _],
    [_, _, INK, INK, _, _],
]


def _tdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def project_ball(cx, cy, angle, r_q8, z_q8, n, snap=False):
    """Section 3.4: (x, y, ys, sin). angle is in pocket units (n << 16 a turn)."""
    a16 = (angle % (n << 16)) // n                 # screen angle, turn-Q16
    if snap:                                       # settled: ride the step grid
        a16 = (a16 & ~255) + 128
    a8, f = a16 >> 8, a16 & 255
    s = isin(a8) + (((isin(a8 + 1) - isin(a8)) * f) >> 8)
    c = isin(a8 + 64) + (((isin(a8 + 65) - isin(a8 + 64)) * f) >> 8)
    if r_q8 >= 52 << 8:
        o = -768
    elif r_q8 > 46 << 8:
        o = -256 - _tdiv((r_q8 - (46 << 8)) * 2, 6)
    else:
        o = 0
    x = cx + ((_tdiv(r_q8 * c, 256) + 128) >> 8)
    ys = cy + (((r_q8 * s >> 9) + o + 128) >> 8)
    y = ys - ((z_q8 * 7) >> 11)
    return x, y, ys, s


TRAIL_MIN = 1311                 # 1.2 rev/s in turn-Q16 per frame
TRAIL = ((2, 2, WHITE), (2, 1, SILVER), (1, 1, SILVER))


def draw_ball(fb, cx, cy, angle, r_q8, z_q8, n, behind=None, snap=False, w=0):
    """Draw the ball, its hop shadow and (above 1.2 rev/s) its trail.

    angle and w (speed per frame) are in pocket units. behind=True draws
    only on the far half (sin < 0), behind=False only on the near half,
    None always. Returns True when drawn.
    """
    x, y, ys, s = project_ball(cx, cy, angle, r_q8, z_q8, n, snap)
    if behind is not None and (s < 0) != behind:
        return False
    if abs(w) > TRAIL_MIN * n:                     # 3 ghosts at a - k*w/4
        for k, (gw, gh, c) in enumerate(TRAIL, 1):
            gx, gy, _, _ = project_ball(cx, cy, angle - _tdiv(k * w, 4), r_q8, z_q8, n)
            fb.fill_rect(gx - 1, gy - 1, gw, gh, c)
    if z_q8 > 0:
        fb.hline(x - 2, ys, 3, INK)                # hop shadow on the surface
    fb.sprite(BALL, x - 3, y - 3)
    return True


# ---------------------------------------------------------------------------
# The whole wheel
# ---------------------------------------------------------------------------
def draw_wheel(fb, cx, cy, rotor_q16, n, hi_pocket=None, hi_colour=None,
               clip_x0=0, clip_x1=128, felt=True, ball=None, flash=(), bins=256):
    """Draw the wheel scene into the table band, in section 4's order.

    rotor_q16: rotor angle in pocket units (1 turn = n << 16), Ball.rho.
    hi_pocket: wheel index to highlight in hi_colour (WHITE, then FX_A).
    ball: optional (angle, r_q8, z_q8[, snap[, w]]) - drawn before the
    turret on the far half and after it on the near half.
    flash: deflector indexes flashing WHITE.
    """
    assert cx % 2 == 0 and clip_x0 % 2 == 0 and clip_x1 % 2 == 0
    saved = fb.clip
    fb.set_clip(clip_x0, BAND_Y0, clip_x1 - clip_x0, BAND_Y1 - BAND_Y0)
    if felt:
        draw_felt(fb)
    draw_bowl(fb, cx, cy)
    cls = classes(n)
    if bins == 256:
        lut = build_lut(rotor_q16, n, cls, hi_pocket, hi_colour)
    else:
        lut = build_lut512(rotor_q16, n, cls, hi_pocket, hi_colour)
    ring(fb, cx, cy, lut, clip_x0, clip_x1, BAND_Y0, BAND_Y1, bins)
    if bins == 512:                                # the rings the map can't hold
        for R in (44, 43, 35, 34):
            fb.ellipse(cx, cy, R, R // 2, GOLD)
    # After the ring, not before (section 4 step 3): the two near-side
    # deflectors sit on the rotor's edge row and the ring would hide them.
    # The ring never reaches the other six.
    draw_deflectors(fb, cx, cy, flash)
    draw_cone(fb, cx, cy)
    if ball is not None:
        b_angle, b_r, b_z, snap, w = (tuple(ball) + (False, 0))[:5]
        draw_ball(fb, cx, cy, b_angle, b_r, b_z, n, behind=True, snap=snap, w=w)
    rotor_a8 = ((rotor_q16 % (n << 16)) // n) >> 8
    draw_turret(fb, cx, cy, rotor_a8)
    if ball is not None:
        draw_ball(fb, cx, cy, b_angle, b_r, b_z, n, behind=False, snap=snap, w=w)
    fb.clip = saved


# ---------------------------------------------------------------------------
# Reports and checks
# ---------------------------------------------------------------------------
def geometry_table(cx=CX, cy=CY):
    """Rows each layer spans on the centre column, and how many rows of it
    stay visible at the far (top) and near (bottom) side."""
    layers = [(name, dx, dy, R) for name, dx, dy, R, _, _ in LAYERS[1:]]
    layers.append(("rotor (map)", 0, 0, R_OUT))
    layers.append(("cone", 0, 0, CONE_R))
    out = []
    for k, (name, dx, dy, R) in enumerate(layers):
        top, bot = cy + dy - R // 2, cy + dy + R // 2
        if k + 1 < len(layers):
            _, _, ndy, nR = layers[k + 1]
            far = (cy + ndy - nR // 2) - top
            near = bot - (cy + ndy + nR // 2)
        else:
            far = near = None
        out.append((name, cx + dx, cy + dy, R, R // 2, top, bot, far, near))
    return out


def print_report(bins):
    spans, data = build_map(bins)
    px = sum((1 if i == 0 else 2) * (1 if j == 0 else 2)
             for j, (a, b) in enumerate(spans) for i in range(a, b))
    print(f"angle map ({bins} steps/turn): {len(data)} B map + {2 * len(spans)} B spans"
          f" = {len(data) + 2 * len(spans)} B; orders 37 + 38 = 75 B; {px} ring px/frame")
    if bins == 256:
        counts = [0] * 4
        for v in data:
            counts[v >> 6] += 1
        print("  pixels per band (quadrant): floor %d, number ring %d, mark %d, gold %d" % tuple(counts))
    print("  geometry (centre column):")
    print("    %-12s %-9s %-7s %-9s %s" % ("layer", "centre", "R x ry", "rows", "visible far/near"))
    for name, x, y, R, ry, top, bot, far, near in geometry_table():
        vis = "" if far is None else f"{far} / {near}"
        print(f"    {name:<12} ({x},{y:>3})  {R:>2}x{ry:<3}  {top:>3}..{bot:<4} {vis}")


def self_test():
    verify_orders()
    spans, data = build_map(256)
    # Mirroring: every full-turn step matches the true angle's floor (axes
    # sit on step boundaries and may land either side of them).
    for j, (a, b) in enumerate(spans):
        for i in range(a, b):
            q = quadrant_q(i, j)
            for right in (True, False):
                for down in (True, False):
                    if (not right and i == 0) or (not down and j == 0):
                        continue
                    xx, yy = (i if right else -i), (j if down else -j)
                    true = (math.atan2(2 * yy, xx) % (2 * math.pi)) * 128 / math.pi
                    s = full_step(q, right, down)
                    on_axis = i == 0 or j == 0 or i == 2 * j
                    ok = s == math.floor(true + 1e-12) % 256 or (
                        on_axis and abs((s + 1) % 256 - true % 256) < 1e-9)
                    assert ok, (i, j, right, down, s, true)
    # The packed C loop gives exactly the pixel loop, clipped or not.
    for n in (37, 38):
        lut = build_lut(0x12345 % (n << 16), n, classes(n), 5, FX_A)
        for cx, x0, x1 in ((64, 0, 128), (64, 40, 90), (100, 0, 128), (150, 64, 128),
                           (-20, 0, 50), (32, 30, 34), (64, 64, 66)):
            fb = FB(INK)
            ring(fb, cx, CY, lut, x0, x1, BAND_Y0, BAND_Y1)
            buf = bytearray(64 * 128)
            ring_packed(buf, cx, CY, lut, x0, x1, BAND_Y0, BAND_Y1)
            unpacked = bytes((buf[k >> 1] >> (4 * (k & 1))) & 15 for k in range(128 * 128))
            assert unpacked == bytes(fb.p), (n, cx, x0, x1)
    # The LUT: frets exactly 2 steps, so each pocket shows 4 or 5 floor
    # steps as the rotor turns; the zero's floor is FELT_DK (green felt).
    for n in (37, 38):
        cls = classes(n)
        for rho in range(0, n << 16, 4099):
            lut = build_lut(rho, n, cls)
            floor = [lut[((s >> 6) << 8) + ((63 - (s & 63)) if s & 64 else (s & 63))]
                     for s in range(256)]
            i0 = floor.index(SILVER)
            runs, run = [], 0
            for c in floor[i0:] + floor[:i0] + [SILVER]:   # one turn, from a fret
                if c == SILVER:
                    if run:
                        runs.append(run)
                    run = 0
                else:
                    run += 1
            assert set(runs) <= {4, 5}, (n, rho, sorted(set(runs)))
        lut = build_lut(0, n, cls)
        assert lut[0] == SILVER and lut[3] == FELT_DK
    # Near-side tucking (1.1): no inner layer pokes out below an outer one,
    # and the track and apron keep at least 1 row on every column.
    stack = [(dy, R) for _, _, dy, R, _, _ in LAYERS[1:]] + [(0, R_OUT), (0, CONE_R)]
    for (dy1, R1), (dy2, R2) in zip(stack, stack[1:]):
        for dx in range(R2 + 1):
            b1 = dy1 + max(j for j in range(R1 // 2 + 1) if ell(R1)[j] >= dx)
            b2 = dy2 + max(j for j in range(R2 // 2 + 1) if ell(R2)[j] >= dx)
            assert b2 <= b1 - (1 if R1 in (54, 50) else 0), (R1, R2, dx)
    print("self-test: orders, mirroring, packed ring loop, LUT frets, tucking OK")


# ---------------------------------------------------------------------------
# Previews
# ---------------------------------------------------------------------------
def scene(n, rho, **kw):
    fb = FB(NAVY)
    fb.fill_rect(0, 0, 128, WALL_H, NAVY)
    draw_wheel(fb, CX, CY, rho, n, **kw)
    return fb


def turns(n, t):
    """t turns in pocket units."""
    return int(round(t * (n << 16))) % (n << 16)


def pocket_centre_angle(rho, p, n):
    return (rho + (p << 16) + 0x8000) % (n << 16)


def previews():
    from PIL import Image
    OUT.mkdir(parents=True, exist_ok=True)
    written = []

    def save(fb, name, scale=3, **kw):
        path = OUT / name
        fb.save(path, scale, **kw)
        written.append(path)
        return path

    angles = (0.0, 0.2913, 0.6402)
    sheet = Image.new("RGB", (3 * 384, 2 * 384))
    for row, (n, tag) in enumerate(((37, "eu"), (38, "us"))):
        for col, t in enumerate(angles):
            fb = scene(n, turns(n, t))
            save(fb, f"wheel_{tag}_{col}.png")
            sheet.paste(fb.image(3), (col * 384, row * 384))
    sheet_path = OUT / "wheel_sheet.png"
    sheet.save(sheet_path)
    written.append(sheet_path)

    # Ball on the track: far side at the flick point, near side.
    rho = turns(37, 0.11)
    w = -2294 * 37                                 # 2.1 rev/s, counter-clockwise: trail shows
    save(scene(37, rho, ball=(turns(37, 176 / 256), 52 << 8, 0, False, w)), "wheel_ball_far.png")
    save(scene(37, rho, ball=(turns(37, 0.31), 52 << 8, 0)), "wheel_ball_near.png")
    # Mid-hop over the rotor (near side, 5 px up) and on the far side.
    save(scene(37, rho, ball=(turns(37, 0.20), 38 << 8, 5 << 8)), "wheel_hop.png")
    save(scene(37, rho, ball=(turns(37, 0.70), 36 << 8, 4 << 8)), "wheel_hop_far.png")
    # Settled in 17 (European), near side, pocket highlighted FX_A.
    n, p = 37, EU_ORDER.index(17)
    rho = (turns(n, 0.22) - (p << 16) - 0x8000) % (n << 16)
    ball = (pocket_centre_angle(rho, p, n), R_REST << 8, 0, True)
    save(scene(n, rho, hi_pocket=p, hi_colour=FX_A, ball=ball), "wheel_settled_eu17.png")
    # Settled in 00 (American), far side behind the turret, WHITE flash frame.
    n, p = 38, US_ORDER.index(N00)
    rho = (turns(n, 0.76) - (p << 16) - 0x8000) % (n << 16)
    ball = (pocket_centre_angle(rho, p, n), R_REST << 8, 0, True)
    save(scene(n, rho, hi_pocket=p, hi_colour=WHITE, ball=ball), "wheel_settled_us00.png")
    # Zoom on the rotor (6x of a native crop).
    fb = scene(37, turns(37, 0.2913))
    crop = fb.image(1).crop((14, 58, 114, 114)).resize((600, 336), Image.NEAREST)
    zpath = OUT / "wheel_rotor_zoom.png"
    crop.save(zpath)
    written.append(zpath)
    # Mid swing: wheel coming in from the right, clipped to the visible strip.
    off = 80
    fb = FB(NAVY)
    fb.fill_rect(0, BAND_Y0, 128 - off, BAND_Y1 - BAND_Y0, FELT)
    draw_wheel(fb, CX + 128 - off, CY, turns(37, 0.4), 37, clip_x0=128 - off, clip_x1=128)
    save(fb, "wheel_swing.png")
    # The 512-step variant, for comparison.
    save(scene(37, turns(37, 0.2913), bins=512), "wheel_eu_bins512.png")
    return written


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bins", type=int, choices=(256, 512), default=256)
    ap.add_argument("--no-png", action="store_true")
    args = ap.parse_args()
    self_test()
    print_report(args.bins)
    size, span = write_c(ASSETS / "WheelMap.h", ASSETS / "WheelMap.cpp", args.bins)
    print(f"wrote {ASSETS / 'WheelMap.h'} and WheelMap.cpp ({size} + {span} B)")
    if not args.no_png:
        for path in previews():
            print("wrote", path)


if __name__ == "__main__":
    main()
