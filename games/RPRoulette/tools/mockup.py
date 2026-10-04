"""P0 mockups: the play screen and friends, drawn with pixkit at the
geometry in docs/design/layout.md and wheel.md, written to out/mockups/.

    python tools/mockup.py            # every scene
    python tools/mockup.py betting    # scenes whose name contains 'betting'
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))   # the repository's tools/: pixkit
import pixkit as k  # noqa: E402
from pixkit import (INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE, GOLD, WOOD,
                    BLUE, NAVY, SKIN, CYAN, FX_A, FX_B)

OUT = HERE.parent / "out" / "mockups"

RED_NUMS = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36}
ZERO = FELT_LT          # zero cell fill (green felt only)


# ---------------------------------------------------------------------------
# Wall band (rows 0..45): CHBlackjack's wall, dealer, plaque/bubble, rail,
# and the tote board where the shoe was.
# ---------------------------------------------------------------------------
def wall(fb):
    for y in range(42):
        fb.hline(0, y, 128, NAVY)
        for x in range(3, 128, 8):
            fb.pixel(x, y, INK)
    fb.dither(0, 0, 128, 3, INK, 0)
    fb.dither(8, 2, 36, 30, WOOD, 1)


def dealer(fb, alt=False):
    remap = None
    if alt:
        remap = list(range(16))
        remap[RED], remap[WINE], remap[WOOD] = GOLD, NAVY, INK
    fb.sprite(k.DEALER, 2, 0, remap)


def fmt_money(v):
    return ("-$" if v < 0 else "$") + str(abs(v))


def plaque(fb, purse, bet, flash=False):
    x, y, w, h = 52, 3, 48, 34
    fb.panel(x, y, w, h, 3, INK, GOLD)
    fb.text35(x + 4, y + 3, "PURSE", FELT_LT)
    s = fmt_money(purse)
    tw = k.text57_width(s)
    c = WHITE if flash else GOLD
    fb.text57(x + w - 4 - tw, y + 10, s, c)
    fb.text57(x + w - 3 - tw, y + 10, s, c)
    fb.hline(x + 3, y + 19, w - 6, NAVY)
    fb.text35(x + 4, y + 22, "BET", SILVER)
    s = fmt_money(bet)
    fb.text57(x + w - 4 - k.text57_width(s), y + 22, s, WHITE)


def bubble(fb, text, typed=999):
    x, y, w, h = 50, 2, 51, 35
    fb.panel(x, y, w, h, 4, WHITE, INK)
    for i in range(5):
        fb.hline(x - 5 + i, y + 22 + i, 6 - i, WHITE)
        fb.pixel(x - 6 + i, y + 22 + i, INK)
    fb.vline(x, y + 21, 4, WHITE)
    lines = text.split("\n")
    ty = y + h // 2 - (len(lines) * 7) // 2 + 1
    for line in lines:
        lx = x + w // 2 - k.text35_width(line) // 2
        fb.text35(lx, ty, line[:max(0, typed)], INK)
        typed -= len(line) + 1
        ty += 7


def tote(fb, history):
    """Last results, newest on top: red right, black left, zero centred."""
    x, y, w, h = 103, 3, 23, 37
    fb.panel(x, y, w, h, 3, INK, GOLD)
    for i, n in enumerate(history[:5]):
        s = "00" if n == 37 else str(n)
        ty = y + 4 + i * 6 + (1 if i else 0)
        if n in (0, 37):
            fb.text35(x + w // 2 - k.text35_width(s) // 2, ty, s, FELT_LT)
        elif n in RED_NUMS:
            fb.text35(x + w - 4 - k.text35_width(s), ty, s, RED)
        else:
            fb.text35(x + 4, ty, s, WHITE)
        if i == 0:
            fb.hline(x + 3, ty + 6, w - 6, NAVY)


def rail(fb):
    fb.hline(0, 42, 128, GOLD)
    fb.fill_rect(0, 43, 128, 2, WOOD)
    fb.hline(0, 45, 128, INK)
    rack = [WHITE, RED, BLUE, FELT_LT, INK]
    fb.fill_rect(53, 42, 46, 4, INK)
    for i in range(11):
        c = rack[i % 5]
        fb.fill_rect(54 + i * 4, 42, 3, 3, c)
        fb.pixel(55 + i * 4, 43, SILVER if c == WHITE else WHITE)


def wall_band(fb, purse=500, bet=0, say=None, history=(17, 32, 0, 5, 26), alt=False):
    wall(fb)
    dealer(fb, alt)
    if say:
        bubble(fb, say)
    else:
        plaque(fb, purse, bet)
    tote(fb, list(history))
    rail(fb)


# ---------------------------------------------------------------------------
# Betting layout (rows 46..127): docs/design/layout.md section 1.
# ---------------------------------------------------------------------------
def cell(n):
    """Inside rect (x0, y0, x1, y1) of number n (1..36)."""
    c, r = (n - 1) // 3 + 1, (n - 1) % 3
    x0, y0 = 9 * c, 69 - 10 * r
    return x0, y0, x0 + 7, y0 + 8


def zero_cells(us):
    if us:
        return {0: (1, 64, 7, 77), 37: (1, 49, 7, 62)}
    return {0: (1, 49, 7, 77)}


def kern21(fb, x, y, c):
    fb.text35(x, y, "2", c)
    fb.pixel(x + 4, y + 1, c)
    fb.pixel(x + 4, y + 3, c)
    fb.text35(x + 6, y, "1", c)


def layout(fb, us=False, zero_digit=INK):
    fb.fill_rect(0, 46, 128, 82, FELT)
    fb.hline(0, 46, 128, FELT_DK)
    # Number cells.
    for n in range(1, 37):
        x0, y0, x1, y1 = cell(n)
        fb.fill_rect(x0, y0, 8, 9, RED if n in RED_NUMS else INK)
        fb.text35(x0 + (3 if n < 10 else 1), y0 + 2, str(n), WHITE)
    for n, (x0, y0, x1, y1) in zero_cells(us).items():
        fb.fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, ZERO)
        s = "00" if n == 37 else "0"
        ty = (y0 + y1) // 2 - 2
        fb.text35(4 - k.text35_width(s) // 2, ty, s, zero_digit)
    if us:
        fb.hline(0, 63, 9, GOLD)
    # Column bets.
    for kk in range(3):
        kern21(fb, 118, 69 - 10 * kk + 2, WHITE)
    # Dozens.
    for kk, s in enumerate(("1st 12", "2nd 12", "3rd 12")):
        x0 = 9 + 36 * kk
        fb.text35(x0 + 6, 81, s, WHITE)
    # Even money.
    fb.text35(10, 91, "1-18", WHITE)
    fb.text35(28, 91, "EVEN", WHITE)
    fb.text35(84, 91, "ODD", WHITE)
    fb.text35(99, 91, "19", WHITE)
    fb.hline(107, 93, 2, WHITE)
    fb.text35(109, 91, "36", WHITE)
    for cx, col in ((53, RED), (71, INK)):
        for i, hw in enumerate((1, 3, 5, 3, 1)):
            fb.hline(cx - hw + 1, 91 + i, 2 * hw - 1, col)
            fb.pixel(cx - hw, 91 + i, GOLD)
            fb.pixel(cx + hw, 91 + i, GOLD)
        fb.pixel(cx, 90, GOLD)
        fb.pixel(cx, 96, GOLD)
    # Gold grid.
    fb.hline(0, 48, 128, GOLD)
    fb.hline(0, 78, 128, GOLD)
    fb.hline(8, 58, 120, GOLD)
    fb.hline(8, 68, 120, GOLD)
    fb.hline(8, 88, 109, GOLD)
    fb.hline(8, 98, 109, GOLD)
    for x in [0, 127] + [8 + 9 * kk for kk in range(13)]:
        fb.vline(x, 48, 31, GOLD)
    for x in (8, 44, 80, 116):
        fb.vline(x, 78, 21, GOLD)
    for x in (26, 62, 98):
        fb.vline(x, 88, 11, GOLD)
    fb.hline(0, 111, 128, FELT_DK)


def ring(fb, x0, y0, x1, y1, c=FX_B):
    """Highlight a cell: its grid lines plus the inner top and bottom rows."""
    fb.rect(x0 - 1, y0 - 1, x1 - x0 + 3, y1 - y0 + 3, c)
    fb.hline(x0, y0, x1 - x0 + 1, c)
    fb.hline(x0, y1, x1 - x0 + 1, c)


# Lattice anchors.
def XL(u):
    return (9 * u + 16 + (u & 1)) // 2


def YL(v):
    return 78 - 5 * v


def anchor_of(n):
    x0, y0, _, _ = cell(n)
    return x0 + 4, y0 + 4


# ---------------------------------------------------------------------------
# Mini chips: 7 px wide, one row per chip, up to 4 shown.
# ---------------------------------------------------------------------------
def mini_stack(fb, x, y, amount, outline=None):
    chips = []
    a = amount
    for d in range(4, -1, -1):
        while a >= k.CHIP_VALUE[d]:
            chips.append(d)
            a -= k.CHIP_VALUE[d]
    if not chips:
        return
    d = chips[0]
    n = min(4, len(chips))
    b, e, sh = k.CHIP_BODY[d], k.CHIP_EDGE[d], k.CHIP_SHADE[d]
    o = outline if outline is not None else (GOLD if d == 4 else INK)
    fb.hline(x - 2, y - n - 1, 5, o)
    for yy in (y - n, y - n + 1):
        fb.pixel(x - 3, yy, o)
        fb.pixel(x + 3, yy, o)
        fb.hline(x - 2, yy, 5, b)
    fb.pixel(x, y - n, e)
    for i in range(n):
        yy = y - n + 2 + i
        fb.pixel(x - 3, yy, o)
        fb.pixel(x + 3, yy, o)
        for dx in (-2, 0, 2):
            fb.pixel(x + dx, yy, sh)
        for dx in (-1, 1):
            fb.pixel(x + dx, yy, e)
    fb.hline(x - 2, y + 2, 5, o)


def ghost(fb, x, y):
    fb.hline(x - 2, y - 2, 5, FX_B)
    fb.hline(x - 2, y + 2, 5, FX_B)
    fb.vline(x - 3, y - 1, 3, FX_B)
    fb.vline(x + 3, y - 1, 3, FX_B)


def glove(fb, x, y, remap=None):
    """Fingertip 4 px above the spot's anchor."""
    tx, ty = x, y - 4
    fb.sprite(k.HAND, tx - k.HAND_TIP, ty - 15, remap)


# ---------------------------------------------------------------------------
# Plate and bar
# ---------------------------------------------------------------------------
def plate(fb, words, y=100):
    tw = sum(k.text35_width(w) + (1 if w.endswith(" ") else 0) for w, _ in words)
    tw = k.text35_width("".join(w for w, _ in words))
    pw = tw + 8
    fb.fill_round(64 - pw // 2, y, pw, 11, 2, NAVY)
    fb.round_rect(64 - pw // 2, y, pw, 11, 2, GOLD)
    x = 64 - tw // 2
    for w, c in words:
        fb.text35(x, y + 3, w, c)
        x += 4 * len(w)


def dark_face(c):
    return c in (RED, BLUE, NAVY, WINE, INK)


def button(fb, x, w, face, label, sel=False):
    y, h = 114 - (1 if sel else 0), 12
    fb.fill_round(x, y, w, h, 3, face)
    fb.hline(x + 2, y + h - 2, w - 4, INK if dark_face(face) else WOOD)
    fb.round_rect(x, y, w, h, 3, FX_B if sel else INK)
    tc = WHITE if dark_face(face) else INK
    if sel and k.text57_width(label) <= w - 4:
        fb.text57(x + w // 2 - k.text57_width(label) // 2, y + 3, label, tc)
    else:
        fb.text35(x + w // 2 - k.text35_width(label) // 2, y + 4, label, tc)


def bar(fb, active=2, hover=None):
    """CLR | $1 $5 $10 $25 $100 | SPIN. hover: 0 CLR, 1..5 chips, 6 SPIN."""
    fb.fill_rect(0, 112, 128, 16, NAVY)
    fb.hline(0, 112, 128, INK)
    button(fb, 0, 17, SILVER, "CLR", hover == 0)
    for i, v in enumerate(("$1", "$5", "$10", "$25", "$100")):
        x, w = 18 + 17 * i, 16
        sel = hover == i + 1
        y = 114 - (1 if sel else 0)
        cx = x + w // 2
        if active == i:
            fb.fill_round(x, y - 1, w, 14, 3, FX_B)
        k.chip(fb, cx, y + 1, i, True)
        fb.text35(cx - k.text35_width(v) // 2, y + 7, v, INK if active == i else WHITE)
    button(fb, 104, 24, GOLD, "SPIN", hover == 6)


# ---------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------
BETS_A = [  # (anchor x, anchor y, amount)
    (4, 63, 25),                 # 0
    (XL(2), YL(1), 5),           # split 1/4
    (XL(13), YL(5), 10),         # straight 21? (u=13 col 7, v=5 top row)
    (35, 93, 50),                # EVEN
    (62, 83, 25),                # 2nd 12
]


def betting(name, hover, plate_words, us=False, bets=BETS_A, glove_at=None, ghost_at=None,
            purse=410, bet=90, active=2, say=None, rings=(), bar_hover=None):
    fb = k.FB()
    wall_band(fb, purse, bet, say=say)
    layout(fb, us)
    for x0, y0, x1, y1 in rings:
        ring(fb, x0, y0, x1, y1)
    for x, y, a in bets:
        mini_stack(fb, x, y, a)
    if ghost_at:
        ghost(fb, *ghost_at)
    if plate_words:
        plate(fb, plate_words)
    bar(fb, active, bar_hover)
    if glove_at:
        glove(fb, *glove_at)
    fb.save(OUT / f"{name}.png", fx_b=0xFFF)        # FX_B at the top of its pulse
    return fb


def scene_betting_split():
    # Glove on SPLIT 17/20 holding $10: both cells ringed, ghost chip on the line.
    gx, gy = XL(12), YL(3)
    betting("betting_split", None,
            [("SPLIT", WHITE), (" 17/20", WHITE), (" $10", GOLD), (" 17 TO 1", SILVER)],
            rings=[cell(17), cell(20)], glove_at=(gx, gy),
            bets=BETS_A + [(gx, gy, 10)])


def scene_betting_corner():
    gx, gy = XL(12), YL(4)        # corner 17/18/20/21
    betting("betting_corner", None,
            [("CORNER", WHITE), (" 17/18/20/21", WHITE), (" 8 TO 1", SILVER)],
            rings=[cell(17), cell(18), cell(20), cell(21)], glove_at=(gx, gy),
            ghost_at=(gx, gy))


def scene_betting_red():
    betting("betting_red", None,
            [("RED", WHITE), (" $25", GOLD), (" 1 TO 1", SILVER)],
            rings=[cell(n) for n in sorted(RED_NUMS)], glove_at=(53, 93),
            bets=BETS_A + [(53, 93, 25)])


def scene_betting_straight():
    ax, ay = anchor_of(5)
    betting("betting_straight", None,
            [("STRAIGHT", WHITE), (" 5", WHITE), (" $5", GOLD), (" 35 TO 1", SILVER)],
            rings=[cell(5)], glove_at=(ax, ay), bets=BETS_A + [(ax, ay, 5)])


def scene_betting_us():
    betting("betting_american", None,
            [("TOP LINE", WHITE), (" 0/00/1/2/3", WHITE), (" 6 TO 1", SILVER)],
            us=True, rings=[cell(1), cell(2), cell(3), (1, 64, 7, 77), (1, 49, 7, 62)],
            glove_at=(XL(0), YL(0)), ghost_at=(XL(0), YL(0)),
            bets=[(4, 55, 5), (XL(12), YL(3), 10), (53, 93, 25)])


def scene_betting_bar():
    betting("betting_bar_spin", None, [("SPIN", GOLD), (" $90 ON 5 BETS", WHITE)],
            glove_at=(115, 123), bar_hover=6)


def scene_nomorebets():
    betting("nomorebets", None, None, say="NO MORE\nBETS!", glove_at=None)


# ---------------------------------------------------------------------------
# Payout
# ---------------------------------------------------------------------------
DOLLY = ["..k..",
         ".kyk.",
         "kyyyk",
         "kwwsk",
         "kwwsk",
         "kwwsk",
         "kwwsk",
         "kswsk",
         ".kkk."]
_DC = {"k": INK, "y": GOLD, "w": WHITE, "s": SILVER}


def dolly(fb, x, y):
    """Standing on the anchor (x, y): base row at y + 2."""
    for j, row in enumerate(DOLLY):
        for i, ch in enumerate(row):
            if ch != ".":
                fb.pixel(x - 2 + i, y + 2 - 8 + j, _DC[ch])


def float_text(fb, x, y, s, c):
    w = k.text35_width(s)
    fb.text35(x - w // 2 + 1, y + 1, s, INK)
    fb.text35(x - w // 2, y, s, c)


CONF = [RED, GOLD, FELT_LT, CYAN, BLUE, WHITE]


def confetti(fb, n, seed, y0=46, y1=110):
    import random
    r = random.Random(seed)
    for _ in range(n):
        x, y, c = r.randrange(4, 124), r.randrange(y0, y1), r.choice(CONF)
        if r.random() < 0.5:
            fb.hline(x, y, 2, c)
        else:
            fb.vline(x, y, 2, c)


def coins(fb, n, seed, y0=50, y1=120):
    import random
    r = random.Random(seed)
    for _ in range(n):
        x, y = r.randrange(6, 122), r.randrange(y0, y1)
        if r.random() < 0.3:
            fb.vline(x + 1, y, 3, GOLD)
        else:
            fb.fill_rect(x, y, 3, 3, GOLD)
            fb.pixel(x + 1, y + 1, WOOD)


def stars(fb, pts):
    for x, y, c in pts:
        fb.hline(x - 1, y, 3, c)
        fb.vline(x, y - 1, 3, c)


def scene_payout():
    """17 black came in: the dolly is on 17, losers are gone, the split
    17/20 is paid ($170 beside the $10 stake) and one more chip is in the air."""
    fb = k.FB()
    wall_band(fb, purse=410, bet=10, history=(17, 17, 32, 0, 5), alt=False)
    layout(fb)
    ring(fb, *cell(17), c=FX_A)
    gx, gy = XL(12), YL(3)
    mini_stack(fb, gx, gy, 10)
    mini_stack(fb, gx + 8, gy, 170)                  # the winnings, beside the stake
    ax, ay = anchor_of(17)
    dolly(fb, ax, ay)
    k.chip(fb, 70, 52, 3, True)                      # a $25 on its way from the rack
    float_text(fb, gx + 4, 50, "+$170", GOLD)
    plate(fb, [("17", WHITE), (" BLACK", SILVER), (" YOU WIN", GOLD), (" $180", GOLD)])
    bar(fb, 2)
    fb.save(OUT / "payout.png", fx_b=0xFFF)


def scene_dolly():
    """The croupier's red glove taps the dolly onto the winning number."""
    fb = k.FB()
    wall_band(fb, purse=320, bet=90, say="17 BLACK", history=(17, 32, 0, 5, 26))
    layout(fb)
    ring(fb, *cell(17), c=FX_A)
    for x, y, a in BETS_A + [(XL(12), YL(3), 10)]:
        mini_stack(fb, x, y, a)
    ax, ay = anchor_of(17)
    dolly(fb, ax, ay)
    plate(fb, [("17", WHITE), (" BLACK", SILVER), (" ODD", SILVER), (" 2nd 12", SILVER)])
    bar(fb, 2)
    fb.sprite(k.HAND, ax - k.HAND_TIP, ay - 8 - 15 + 1, k.RM_CPU)
    fb.save(OUT / "dolly.png", fx_b=0xFFF)


def scene_bigwin():
    """A straight-up hit: rainbow banner over the layout, confetti and coins."""
    fb = k.FB()
    wall_band(fb, purse=4010, bet=100, history=(17, 32, 0, 5, 26))
    layout(fb)
    ring(fb, *cell(17), c=FX_A)
    ax, ay = anchor_of(17)
    mini_stack(fb, ax, ay, 100)
    dolly(fb, ax + 6, ay)
    fb.dither(0, 47, 128, 64, INK, 0)                # veil: the banner owns the moment
    confetti(fb, 40, 3)
    coins(fb, 10, 4)
    stars(fb, [(30, 60, FX_A), (98, 70, FX_A), (64, 104, WHITE)])
    k.banner(fb, "35 TO 1!", k.B_RAINBOW, 70)
    float_text(fb, 64, 88, "+$3500", GOLD)
    bar(fb, 4)
    fb.save(OUT / "bigwin.png", fx_b=0xFFF)


def scene_result_banners():
    """The three result calls side by side on the felt (cy 58, over the far rim)."""
    for name, text, style in (("result_red", "32 RED", k.B_RED),
                              ("result_black", "17 BLACK", k.B_BLACK),
                              ("result_zero", "0 GREEN", k.B_GREEN)):
        fb = k.FB()
        wall_band(fb, purse=410, bet=90, say=text.replace(" ", "\n", 1) if False else None)
        fb.fill_rect(0, 46, 128, 82, FELT)
        fb.hline(0, 46, 128, FELT_DK)
        k.banner(fb, text, style, 58)
        fb.save(OUT / f"{name}.png", fx_b=0xFFF)


# ---------------------------------------------------------------------------
# The wheel: title, spin, whip, result
# ---------------------------------------------------------------------------
import wheel as wh


def wheel_scene(fb, rho, n=37, cy=wh.CY, ball=None, hi=None, hi_c=FX_A, y0=None, felt=True):
    """wheel.draw_wheel, optionally into a band starting at row y0."""
    old = wh.BAND_Y0
    if y0 is not None:
        wh.BAND_Y0 = y0
    try:
        wh.draw_wheel(fb, wh.CX, cy, rho, n, hi_pocket=hi, hi_colour=hi_c, ball=ball, felt=felt)
    finally:
        wh.BAND_Y0 = old


def felt_backdrop(fb):
    fb.clear(FELT)
    fb.dither(0, 0, 128, 6, FELT_DK, 0)
    fb.dither(0, 122, 128, 6, FELT_DK, 1)
    fb.dither(0, 0, 6, 128, FELT_DK, 0)
    fb.dither(122, 0, 6, 128, FELT_DK, 1)
    fb.rect(2, 2, 124, 124, GOLD)


def logo_bits():
    rows = [ln.rstrip() for ln in (HERE / "art" / "logo.txt").read_text().splitlines()
            if ln.strip() and not ln.startswith("# ")]
    return rows


def title(name, logo):
    fb = k.FB()
    felt_backdrop(fb)
    fb.dither(24, 26, 80, 70, FELT_LT, 0)                       # spotlight on the wheel
    n, rho = 37, wh.turns(37, 0.37)
    wheel_scene(fb, rho, n, cy=60, y0=0, felt=False,
                ball=(wh.turns(37, 0.62), 52 << 8, 0, False, -1400 * 37))
    if logo == "A":
        rows = logo_bits()
        m = k.Mask(len(rows[0]), len(rows))
        m.blit(rows)
        ramp = [FX_B if i < 3 else (GOLD if i < 12 else WOOD) for i in range(len(rows) + 2)]
        m.draw(fb, 64 - len(rows[0]) // 2, 6, GOLD, INK, WINE, ramp)
    else:
        k.title35(fb, "ROULETTE", 5, 3, FX_B, GOLD, WOOD, WINE, 9)
    fb.dither(0, 95, 128, 27, INK, 1)
    items = ("PLAY", "OPTIONS", "STATS")
    for i, s in enumerate(items):
        y = 97 + i * 10
        w = k.text57_width(s)
        if i == 0:
            fb.panel(64 - w // 2 - 6, y - 2, w + 12, 11, 3, NAVY, FX_B)
        fb.text57(64 - w // 2, y, s, GOLD if i == 0 else WHITE)
    k.chip_stack(fb, 15, 116, 95, 6)
    k.chip_stack(fb, 113, 116, 155, 6)
    fb.save(OUT / f"{name}.png", fx_b=0xFFF)


def scene_title_a():
    title("title_logo", "A")


def scene_title_b():
    title("title_title35", "B")


def scene_spin():
    """No more bets: the croupier has flicked the ball, it races round the track."""
    fb = k.FB()
    wall_band(fb, purse=410, bet=90, say="NO MORE\nBETS!")
    rho = wh.turns(37, 0.15)
    wheel_scene(fb, rho, ball=(wh.turns(37, 0.80), 52 << 8, 0, False, -2294 * 37))
    fb.sprite(k.HAND, 14, 50, k.RM_CPU)                          # the croupier's glove at the flick point
    fb.save(OUT / "spin.png", fx_b=0xFFF)


def scene_result():
    """17 black: the pocket flashes, the number pops out over the far rim."""
    fb = k.FB()
    wall_band(fb, purse=410, bet=90, say="17 BLACK")
    n, p = 37, wh.EU_ORDER.index(17)
    rho = (wh.turns(n, 0.22) - (p << 16) - 0x8000) % (n << 16)
    ball = (wh.pocket_centre_angle(rho, p, n), wh.R_REST << 8, 0, True)
    wheel_scene(fb, rho, n, ball=ball, hi=p)
    stars(fb, [(44, 100, WHITE), (70, 108, GOLD), (56, 112, GOLD)])
    k.banner(fb, "17 BLACK", k.B_BLACK, 58)
    fb.save(OUT / "result_wheel.png", fx_b=0xFFF)


def scene_result_zero():
    fb = k.FB()
    wall_band(fb, purse=410, bet=90, say="ZERO!")
    n, p = 37, 0
    rho = (wh.turns(n, 0.70) - (p << 16) - 0x8000) % (n << 16)
    ball = (wh.pocket_centre_angle(rho, p, n), wh.R_REST << 8, 0, True)
    wheel_scene(fb, rho, n, ball=ball, hi=p)
    k.banner(fb, "0 GREEN", k.B_GREEN, 58)
    fb.save(OUT / "result_wheel_zero.png", fx_b=0xFFF)


def scene_whip():
    """Three frames of the whip from the layout to the wheel, side by side."""
    from PIL import Image
    import random
    bet = betting("_whip_layout", None, None, say="NO MORE\nBETS!")
    wfb = k.FB()
    wheel_scene(wfb, wh.turns(37, 0.15))
    strip = Image.new("RGB", (3 * 384 + 2 * 12, 384), (24, 24, 28))
    r = random.Random(7)
    for i, off in enumerate((24, 64, 104)):
        fb = k.FB()
        fb.p[:] = bet.p
        for y in range(46, 128):
            for x in range(128):
                sx = x + off
                fb.p[y * 128 + x] = bet.p[y * 128 + sx] if sx < 128 else wfb.p[y * 128 + sx - 128]
        speed = 40 - abs(64 - off) // 2
        for _ in range(6):
            yy, ln = r.randrange(47, 127), 2 * speed // 3 + r.randrange(16)
            fb.hline(r.randrange(0, 128 - ln), yy, ln, r.choice((WHITE, SILVER, FELT_LT)))
        strip.paste(fb.image(3, fx_b=0xFFF), (i * 396, 0))
    strip.save(OUT / "whip_strip.png")
    (OUT / "_whip_layout.png").unlink()


SCENES = {n[6:]: f for n, f in globals().items() if n.startswith("scene_")}


def main():
    pick = sys.argv[1] if len(sys.argv) > 1 else ""
    for name, f in SCENES.items():
        if pick in name:
            f()
            print("wrote", name)


if __name__ == "__main__":
    main()
