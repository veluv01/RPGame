"""The SHARK's table: from each square, how many ARCADE turns the best
play still needs to reach 100, on average (in eighths of a turn).

    python tools/turns.py          # prints the table for Cpu.cpp

Reads the ladders and snakes from Layout.cpp. The host tests work
the same table out again and compare (tools/tests/test_rules.cpp).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def links():
    src = (ROOT / "src" / "game" / "Layout.cpp").read_text()
    body = src[src.index("LINK[LINKS]"):]
    body = body[:body.index("};")]
    return {int(a): int(b) for a, b in re.findall(r"\{(\d+),\s*(\d+)\}", body)}


def landing(link, n, d):
    n += d
    if n > 100:
        n = 200 - n
    return link.get(n, n)


def solve():
    link = links()
    e = [0.0] * 101
    for _ in range(2000):
        for n in range(99, 0, -1):
            s = 0.0
            for a in range(1, 7):
                for b in range(1, 7):
                    x, y = e[landing(link, n, a)], e[landing(link, n, b)]
                    # A pair moves and rolls again: no turn goes by.
                    s += x - 1 if a == b and x > 0 else min(x, y)
            e[n] = 1 + s / 36
    return e


if __name__ == "__main__":
    e = solve()
    t = [min(255, round(v * 8)) for v in e]
    print(f"// from square 1: {e[1]:.1f} turns")
    for i in range(0, 101, 20):
        print("    " + ", ".join(str(v) for v in t[i:i + 20]) + ",")
