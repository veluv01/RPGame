"""P0 mockups: WORD WHEEL's screens drawn with pixkit at the geometry the
game will use (Layout.h), written to out/mockups/.

    python tools/mockup.py            # every scene
    python tools/mockup.py wheel      # scenes whose name contains 'wheel'
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))   # the repository's tools/: pixkit
import pixkit as k  # noqa: E402
from pixkit import (INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE, GOLD, WOOD,
                    BLUE, NAVY, SKIN, CYAN, FX_A, FX_B)

k.set_upper35(True)                     # the game upper-cases what it draws
OUT = HERE.parent / "out" / "mockups"

VOWELS = "AEIOU"
POD_C = [RED, GOLD, BLUE]
POD_D = [WINE, WOOD, NAVY]


def money(v):
    s = str(abs(v))
    if len(s) > 3:
        s = s[:-3] + "," + s[-3:]
    return ("-$" if v < 0 else "$") + s


def bold57(fb, x, y, s, c):
    fb.text57(x, y, s, c)
    fb.text57(x + 1, y, s, c)


# ---------------------------------------------------------------------------
# Wall band (rows 0..41): the series' wall and dealer; on the right the
# used-letter rack with a status line, or the host's bubble over it.
# ---------------------------------------------------------------------------
def wall(fb):
    for y in range(42):
        fb.hline(0, y, 128, NAVY)
        for x in range(3, 128, 8):
            fb.pixel(x, y, INK)
    fb.dither(0, 0, 128, 3, INK, 0)
    fb.dither(8, 2, 36, 30, WOOD, 1)


def rack(fb, used, status="ROUND 1", right="", wild=False):
    x, y, w, h = 50, 2, 76, 38
    fb.panel(x, y, w, h, 3, INK, GOLD)
    fb.text35(x + 4, y + 3, status, FELT_LT)
    if right:
        fb.text35(x + w - 4 - k.text35_width(right), y + 3, right, GOLD)
    fb.hline(x + 3, y + 10, w - 6, NAVY)
    for i in range(26):
        ch = chr(65 + i)
        cx, cy = x + 4 + (i % 9) * 8, y + 13 + (i // 9) * 8
        if ch in used:
            fb.text35(cx, cy, ch, NAVY)
        else:
            fb.text35(cx, cy, ch, CYAN if ch in VOWELS else WHITE)
    if wild:
        fb.fill_rect(x + 4 + 8 * 8 - 1, y + 13 + 16 - 1, 6, 7, FX_B)
        fb.text35(x + 4 + 8 * 8, y + 13 + 16, "W", INK)


def bubble(fb, text):
    x, y, w, h = 50, 2, 76, 38
    fb.panel(x, y, w, h, 4, WHITE, INK)
    for i in range(5):
        fb.hline(x - 5 + i, y + 22 + i, 6 - i, WHITE)
        fb.pixel(x - 6 + i, y + 22 + i, INK)
    fb.vline(x, y + 21, 4, WHITE)
    lines = text.split("\n")
    ty = y + h // 2 - (len(lines) * 7) // 2 + 1
    for line in lines:
        fb.text35(x + w // 2 - k.text35_width(line) // 2, ty, line, INK)
        ty += 7


def wall_band(fb, used="", say=None, **kw):
    wall(fb)
    fb.sprite(k.DEALER, 2, 0)
    if say:
        bubble(fb, say)
    else:
        rack(fb, used, **kw)


# ---------------------------------------------------------------------------
# Category strip (rows 42..50) and the puzzle board (rows 51..98).
# ---------------------------------------------------------------------------
def strip(fb, text):
    fb.fill_rect(0, 42, 128, 9, NAVY)
    fb.hline(0, 42, 128, GOLD)
    fb.hline(0, 50, 128, GOLD)
    fb.centred35(44, text, WHITE)


ROWS = (12, 14, 14, 12)


def wrap(phrase):
    """Greedy wrap into the board's rows, centred vertically; each row is a
    string placed centred in its row."""
    words = phrase.split()
    for start in (1, 0):
        rows, r, cur = ["", "", "", ""], start, ""
        ok = True
        for w in words:
            t = (cur + " " + w).strip()
            if r < 4 and len(t) <= ROWS[r]:
                cur = t
            else:
                rows[r] = cur
                r += 1
                cur = w
                if r > 3 or len(w) > ROWS[r]:
                    ok = False
                    break
        if ok:
            rows[r] = cur
            used = sum(1 for x in rows if x)
            if start == 1 and used <= 2:
                return rows
            if start == 0 or used <= 3:
                return rows
    raise ValueError(phrase)


def board(fb, phrase, shown, style="A", lit="", cursor=None, typed=None):
    """shown: letters revealed.  lit: letters lighting up (cyan, not yet
    flipped).  cursor: index of the blank under the solve cursor.  typed:
    {blank index: letter} entered during a solve."""
    ph = 10 if style == "A" else 12
    pitch = ph + 1
    y0 = 51
    hgt = 4 * pitch + 4
    fb.fill_rect(0, y0, 128, hgt, INK)
    fb.rect(0, y0, 128, hgt, GOLD)
    rows = wrap(phrase)
    blank = 0
    for r in range(4):
        n = ROWS[r]
        text = rows[r]
        off = (n - len(text)) // 2
        for c in range(14):
            x, y = 1 + 9 * c, y0 + 2 + r * pitch
            if n == 12 and c in (0, 13):
                continue
            i = c - (1 if n == 12 else 0) - off
            ch = text[i] if 0 <= i < len(text) else " "
            if ch == " ":
                fb.fill_rect(x, y, 8, ph, FELT)
                fb.dither(x, y, 8, ph, FELT_DK, (c + r) & 1)
                continue
            is_letter = ch.isalpha()
            if is_letter and ch in lit:
                fb.fill_rect(x, y, 8, ph, CYAN)
                continue
            fb.fill_rect(x, y, 8, ph, WHITE)
            show = ch
            col = INK
            if is_letter and ch not in shown:
                show = None
                if typed and blank in typed:
                    show, col = typed[blank], BLUE
                if cursor == blank:
                    fb.rect(x - 1, y - 1, 10, ph + 2, FX_B)
                    fb.rect(x, y, 8, ph, RED)
                blank += 1
            if show:
                if style == "A":
                    bold57(fb, x + 1, y + 2, show, col)
                else:
                    fb.text35x2(x + 1, y + 1, show, col)
    return y0 + hgt


def podiums(fb, names, cash, active=0, y=99, h=20, tokens=()):
    for i in range(3):
        x = 1 + 42 * i
        lift = 0
        fb.fill_round(x, y - lift, 41, h, 2, POD_D[i])
        fb.fill_rect(x, y, 41, 7, POD_C[i])
        fb.text35(x + 20 - k.text35_width(names[i]) // 2, y + 1, names[i],
                  INK if i == 1 else WHITE)
        if h < 18:
            fb.fill_rect(x, y + 6, 41, h - 6, POD_D[i])
        s = money(cash[i])
        if len(s) > 6:
            s = s.replace(",", "")
        w = k.text57_width(s) + 1
        bold57(fb, x + 20 - w // 2, y + (9 if h >= 18 else 8), s, WHITE if i != active else FX_B)
        if i == active:
            fb.round_rect(x - 1, y - 1, 43, h + 1, 2, FX_B)
        for t in tokens:
            if t[0] == i:
                fb.fill_rect(x + 34, y + 9, 5, 7, FX_B)
                fb.text35(x + 35, y + 10, t[1], INK)


def prompt(fb, items, sel=0, y=119):
    fb.fill_rect(0, y, 128, 128 - y, INK)
    fb.hline(0, y, 128, GOLD)
    if isinstance(items, str):
        fb.centred35(y + 2, items, WHITE)
        return
    total = sum(k.text35_width(s) for s in items) + 10 * (len(items) - 1)
    x = 64 - total // 2
    for i, s in enumerate(items):
        w = k.text35_width(s)
        if i == sel:
            fb.fill_round(x - 3, y + 1, w + 6, 8, 2, GOLD)
            fb.text35(x, y + 2, s, INK)
        else:
            fb.text35(x, y + 2, s, SILVER)
        x += w + 10


# ---------------------------------------------------------------------------
# Letter picker (slides up over rows 99..127).
# ---------------------------------------------------------------------------
def picker(fb, used, mode="consonant", cur="T", hint="A PICK   B BACK"):
    y = 99
    fb.fill_rect(0, y, 128, 29, INK)
    fb.hline(0, y, 128, GOLD)
    for i in range(26):
        ch = chr(65 + i)
        x, yy = 6 + (i % 13) * 9, y + 3 + (i // 13) * 10
        dead = ch in used or (mode == "consonant" and ch in VOWELS) or \
            (mode == "vowel" and ch not in VOWELS)
        if ch == cur:
            fb.fill_round(x - 2, yy - 2, 10, 11, 2, FX_B)
            bold57(fb, x, yy, ch, INK)
        elif dead:
            fb.text57(x, yy, ch, NAVY if ch in used else WINE)
        else:
            bold57(fb, x, yy, ch, WHITE)
    fb.centred35(y + 23, hint, SILVER)


# ---------------------------------------------------------------------------
# Wheel close-up (rows 42..118): wedges with their values sweeping past the
# pointer.  24 wedges, 42 px each at the rim.
# ---------------------------------------------------------------------------
BANK, LOSE, FREE, TOP = "BANKRUPT", "LOSE", "FREE", "TOP"
WEDGES = [2500, 600, 700, 600, 650, 500, 700, BANK, 600, 550, 500, 600,
          BANK, 650, FREE, 700, LOSE, 800, 500, 650, 500, 900, BANK, 550]
CYCLE = [RED, GOLD, BLUE, CYAN, FELT_LT, SKIN, WINE, SILVER]
DARK = {RED, BLUE, WINE, INK}
PITCH = 42
VY = 250            # the hub the wedges converge on, far below the screen
WY0, WY1 = 51, 118


def wedge_colour(i):
    v = WEDGES[i]
    if v == BANK:
        return INK
    if v == LOSE:
        return WHITE
    if v == FREE:
        return FELT
    if i == 0:
        return FX_B
    return CYCLE[i % 8]


def wx(d, y, flat):
    """Screen x of a point d rim-pixels from the pointer, at row y."""
    if flat:
        return 64 + d
    return 64 + (d * (VY - y)) // (VY - WY0)


def wheel(fb, pos, flat=False, blur=False):
    n = len(WEDGES)
    k0 = (pos - 110) // PITCH
    for y in range(WY0, WY1 + 1):
        for kk in range(k0, k0 + 7):
            xa = wx(kk * PITCH - pos, y, flat)
            xb = wx((kk + 1) * PITCH - pos, y, flat)
            if xb < 0 or xa > 127:
                continue
            c = wedge_colour(kk % n)
            fb.hline(xa + 1, y, xb - xa - 1, c)
            fb.pixel(xa, y, INK)
    if blur:
        fb.dither(0, WY0, 128, WY1 - WY0 + 1, SILVER, 0)
    else:
        for kk in range(k0, k0 + 7):
            i = kk % n
            v = WEDGES[i]
            c = wedge_colour(i)
            ink = WHITE if c in DARK else INK
            mid = kk * PITCH + PITCH // 2 - pos

            def cx(y):
                return wx(mid, y, flat)

            if v == BANK:
                for j, ch in enumerate("BANKRUPT"):
                    y = WY0 + 3 + j * 8
                    bold57(fb, cx(y + 3) - 3, y, ch, WHITE)
            elif v in (LOSE, FREE):
                words = ("LOSE", "A", "TURN") if v == LOSE else ("FREE", "PLAY")
                col = INK if v == LOSE else WHITE
                for j, wd in enumerate(words):
                    y = WY0 + 6 + j * 12
                    w = len(wd) * 8 - 2
                    fb.text35x2(cx(y + 5) - w // 2, y, wd, col)
            else:
                s = "$" + str(v)
                for j, ch in enumerate(s):
                    y = WY0 + 4 + j * 12
                    fb.text35x2(cx(y + 5) - 3, y, ch, ink)
                if i == 0:
                    for sx, sy in ((-9, 8), (7, 30), (-6, 52), (9, 60)):
                        fb.pixel(cx(WY0 + sy) + sx, WY0 + sy, FX_A)
                        fb.pixel(cx(WY0 + sy) + sx + 1, WY0 + sy, WHITE)
    # rim, pegs and the pointer's flipper
    fb.fill_rect(0, 42, 128, 9, NAVY)
    fb.hline(0, 42, 128, GOLD)
    fb.fill_rect(0, 47, 128, 4, WOOD)
    fb.hline(0, 47, 128, GOLD)
    first = -((pos + 64) % 14)
    for x in range(first, 130, 14):
        px = x
        fb.fill_rect(px - 1, 48, 3, 3, FX_B)
        fb.pixel(px, 49, WHITE)
    d = (pos % 14)
    bend = 0 if d < 5 else (2 if d < 10 else 4)
    for j in range(13):
        half = max(0, 5 - j // 2 - (1 if j > 9 else 0))
        ox = (bend * j) // 12
        fb.hline(64 - half + ox, 43 + j, 2 * half + 1, RED if j else WHITE)
        fb.pixel(64 - half + ox - 1, 43 + j, INK)
        fb.pixel(64 + half + ox + 1, 43 + j, INK)
    fb.fill_rect(58, 42, 13, 3, SILVER)
    fb.hline(58, 44, 13, INK)


def wheel_plate(fb, name, cash, i=0):
    s = name + " " + money(cash)
    w = k.text35_width(s) + 8
    fb.fill_round(64 - w // 2, 109, w, 9, 2, POD_C[i])
    fb.round_rect(64 - w // 2 - 1, 108, w + 2, 11, 2, INK)
    fb.centred35(111, s, INK if i == 1 else WHITE)


def power_bar(fb, frac):
    fb.fill_rect(0, 119, 128, 9, INK)
    fb.hline(0, 119, 128, GOLD)
    fb.text35(3, 121, "HOLD A", WHITE)
    fb.rect(32, 121, 92, 6, SILVER)
    w = int(90 * frac)
    fb.fill_rect(33, 122, w, 4, RED)
    fb.fill_rect(33, 122, min(w, 60), 4, GOLD)
    fb.fill_rect(33, 122, min(w, 30), 4, FELT_LT)


# ---------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------
NAMES3 = ("YOU", "DOT", "ACE")
PHRASE = "ONCE IN A BLUE MOON"


def play(name, phrase=PHRASE, cat="PHRASE", shown="NTLE", used="NTLES", style="A", say=None,
         cash=(1850, 600, 0), active=0, sel=0, items=("SPIN", "VOWEL $250", "SOLVE"),
         status="ROUND 1", right="", lit="", **kw):
    fb = k.FB()
    wall_band(fb, used, say, status=status, right=right)
    strip(fb, cat)
    bottom = board(fb, phrase, shown, style, lit=lit, **kw)
    podiums(fb, NAMES3, cash, active, y=bottom, h=119 - bottom)
    prompt(fb, items, sel)
    fb.save(OUT / f"{name}.png")
    return fb


def scene_board_a():
    play("board_A_5x7bold", style="A")


def scene_board_b():
    play("board_B_3x5double", style="B")


def scene_board_long():
    play("board_A_long", phrase="A PENNY FOR YOUR THOUGHTS & A NICKEL FOR MINE",
         cat="BEFORE & AFTER", shown="RSTNE", used="RSTNEK", style="A")
    play("board_B_long", phrase="A PENNY FOR YOUR THOUGHTS & A NICKEL FOR MINE",
         cat="BEFORE & AFTER", shown="RSTNE", used="RSTNEK", style="B")


def scene_reveal():
    play("reveal", shown="NTLE", used="NTLEO", lit="O", say="THREE O'S!",
         cash=(1850, 600, 0), items="+$1,800")


def scene_cpu_turn():
    play("cpu_turn", active=1, say="NO S'S.\nSORRY, DOT.", items="DOT: I'LL SPIN AGAIN",
         used="NTLES")


def scene_picker():
    fb = play("_p", right="$600")
    picker(fb, "NTLES", "consonant", "R", "$600 A LETTER   A PICK")
    fb.save(OUT / "picker_consonant.png")
    fb = play("_p", right="VOWEL")
    picker(fb, "NTLES", "vowel", "O", "VOWEL $250   A BUY  B BACK")
    fb.save(OUT / "picker_vowel.png")
    (OUT / "_p.png").unlink()


def scene_solve():
    fb = play("_p", status="SOLVE", right="0:24", cursor=3, typed={0: "O", 1: "C", 2: "I"})
    picker(fb, "", "solve", "A", "A TYPE  B ERASE  SEL SKIP")
    fb.save(OUT / "solve_entry.png")
    (OUT / "_p.png").unlink()


def scene_tossup():
    play("tossup", phrase="FRESH SQUEEZED LEMONADE", cat="FOOD & DRINK", shown="FSQDLMN",
         used="", say="$1,000\nTOSS-UP!", cash=(0, 0, 0), active=-1,
         items="ANY BUTTON TO BUZZ IN", status="TOSS-UP")


def scene_bonus():
    fb = play("bonus", phrase="QUICK THINKING", cat="PHRASE", shown="RSTLNECH",
              used="RSTLNECHMA", cash=(14650, 3200, 5900), active=0,
              items="A  GOT IT!", status="BONUS", right="0:07")
    fb = play("bonus_pick", phrase="QUICK THINKING", cat="PHRASE", shown="RSTLNE",
              used="RSTLNE", cash=(14650, 3200, 5900), active=0,
              say="3 CONSONANTS\nAND A VOWEL", status="BONUS")
    picker(fb, "RSTLNE", "consonant", "C", "PICK 1 OF 3")
    fb.save(OUT / "bonus_pick.png")


def wheel_scene(name, pos, flat=False, blur=False, say=None, frac=None, who=0, cash=1850,
                right=""):
    fb = k.FB()
    wall_band(fb, "NTLES", say, status="ROUND 1", right=right)
    wheel(fb, pos, flat, blur)
    if frac is not None:
        power_bar(fb, frac)
    else:
        prompt(fb, "A  SPIN!" if who == 0 else NAMES3[who] + " SPINS")
        t = NAMES3[who] + " " + money(cash)
        fb.fill_rect(0, 120, k.text35_width(t) + 5, 8, POD_C[who])
        fb.text35(2, 121, t, INK if who == 1 else WHITE)
    fb.save(OUT / f"{name}.png")


def scene_wheel():
    # pos puts wedge i's middle under the pointer at pos = i*42 + 21
    wheel_scene("wheel_conv_money", 21 + 42 * 4 + 3, right="$650")
    wheel_scene("wheel_flat_money", 21 + 42 * 4 + 3, flat=True, right="$650")
    wheel_scene("wheel_conv_bankrupt", 21 + 42 * 7 - 9, say="OH NO!\nBANKRUPT!")
    wheel_scene("wheel_flat_bankrupt", 21 + 42 * 7 - 9, flat=True, say="OH NO!\nBANKRUPT!")
    wheel_scene("wheel_conv_top", 21 + 42 * 24 + 5, say="$2,500!", who=2, cash=400)
    wheel_scene("wheel_conv_lose", 21 + 42 * 15 + 11, who=1, cash=600)
    wheel_scene("wheel_conv_blur", 21 + 42 * 10, blur=True, frac=0.0)
    wheel_scene("wheel_conv_charge", 21 + 42 * 20, frac=0.72)


def felt_backdrop(fb):
    fb.clear(FELT)
    fb.dither(0, 0, 128, 6, FELT_DK, 0)
    fb.dither(0, 122, 128, 6, FELT_DK, 1)
    fb.dither(0, 0, 6, 128, FELT_DK, 0)
    fb.dither(122, 0, 6, 128, FELT_DK, 1)
    fb.rect(2, 2, 124, 124, GOLD)


def scene_title():
    global WY0, WY1
    fb = k.FB()
    felt_backdrop(fb)
    old = WY0, WY1
    k.title35(fb, "WORD", 6, 5, FX_B, GOLD, WOOD, WINE, 16)
    k.title35(fb, "WHEEL", 34, 5, FX_B, GOLD, WOOD, WINE, 16)
    WY0, WY1 = old
    items = ("PLAY", "OPTIONS", "STATS")
    fb.dither(4, 66, 120, 34, INK, 1)
    for i, s in enumerate(items):
        y = 70 + i * 10
        w = k.text57_width(s)
        if i == 0:
            fb.panel(64 - w // 2 - 6, y - 2, w + 12, 11, 3, NAVY, FX_B)
        fb.text57(64 - w // 2, y, s, GOLD if i == 0 else WHITE)
    # a few letter panels as garnish
    for i, ch in enumerate("SPIN"):
        x = 10 + i * 9
        fb.fill_rect(x, 102, 8, 10, WHITE)
        bold57(fb, x + 1, 104, ch, INK)
    for i, ch in enumerate("SOLVE"):
        x = 74 + i * 9
        fb.fill_rect(x, 102, 8, 10, WHITE if i != 2 else CYAN)
        if i != 2:
            bold57(fb, x + 1, 104, ch, INK)
    fb.centred35(116, "BUILT-IN 700 PUZZLES", FELT_LT)
    fb.save(OUT / "title.png", fx_b=0xFFF)


def scene_summary():
    fb = k.FB()
    felt_backdrop(fb)
    k.title35(fb, "ROUND 2", 8, 3, FX_B, GOLD, WOOD, WINE, 10)
    fb.centred35(30, "DOT SOLVED IT!", WHITE)
    rows = (("YOU", 1850, 4350), ("DOT", 2400, 3400), ("ACE", 0, 1000))
    for i, (n, r, t) in enumerate(rows):
        y = 42 + i * 20
        fb.fill_round(10, y, 108, 17, 3, POD_D[i])
        fb.fill_round(10, y, 30, 17, 3, POD_C[i])
        fb.text35(25 - k.text35_width(n) // 2, y + 6, n, INK if i == 1 else WHITE)
        fb.text35(44, y + 2, "ROUND", SILVER)
        s = money(r if i == 1 else 0)
        fb.text35(114 - k.text35_width(s), y + 2, s, WHITE)
        fb.text35(44, y + 9, "TOTAL", SILVER)
        s = money(t)
        bold57(fb, 113 - k.text57_width(s), y + 8, s, FX_B if i == 0 else WHITE)
    fb.centred35(104, "$1,000 HOUSE MINIMUM", FELT_LT)
    fb.centred35(114, "A  NEXT ROUND", WHITE)
    fb.save(OUT / "summary.png", fx_b=0xFFF)


SCENES = {n[6:]: f for n, f in globals().items() if n.startswith("scene_")}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    pick = sys.argv[1] if len(sys.argv) > 1 else ""
    for name, f in SCENES.items():
        if pick in name:
            f()
            print("wrote", name)


if __name__ == "__main__":
    main()
