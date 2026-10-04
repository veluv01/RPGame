"""Independent reference for the betting layout's spots (Spots.cpp).

    python tools/tests/ref_roulette.py      -> the same lines as `test_rules --dump`

A grid model, not the C++ formulas: every number is a closed rectangle on
the half-cell lattice (number n: column c = (n-1)//3 + 1, row r = (n-1)%3,
spanning u 2c-2..2c and v 2r..2r+2; the zero column spans u -2..0). A
lattice point bets on every cell that touches it: inside a cell a straight,
on an edge a split, at a crossing a corner, on the zero line a split or a
trio. The bottom edge (v = 0) is the street row: a point there bets on whole
columns (a street or a six line), and beside the zero the casino's first
four (European) or top line (American). 0, 00 and 0/00 have their own ids;
the outside bets are the casino's lists.

Line format: "<EU|US> <id> <name>|<numbers>|<payout>|<numbers covered>".
"""

N00 = 37
REDS = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36}
OUTSIDE = ["1st COLUMN", "2nd COLUMN", "3rd COLUMN", "1st DOZEN", "2nd DOZEN", "3rd DOZEN",
           "LOW", "EVEN", "RED", "BLACK", "ODD", "HIGH"]


def cells(us):
    out = []
    for n in range(1, 37):
        c, r = (n - 1) // 3 + 1, (n - 1) % 3
        out.append((n, 2 * c - 2, 2 * c, 2 * r, 2 * r + 2))
    if us:
        out += [(0, -2, 0, 0, 3), (N00, -2, 0, 3, 6)]
    else:
        out.append((0, -2, 0, 0, 6))
    return out


def lattice(u, v, us):
    if v == 0:
        if u == 0:
            return {0, N00, 1, 2, 3} if us else {0, 1, 2, 3}
        cols = [c for c in range(1, 13) if 2 * c - 2 <= u <= 2 * c]
        return {3 * (c - 1) + r + 1 for c in cols for r in range(3)}
    return {n for n, u0, u1, v0, v1 in cells(us) if u0 <= u <= u1 and v0 <= v <= v1}


def outside(k):
    nums = range(1, 37)
    return [
        {n for n in nums if n % 3 == 1}, {n for n in nums if n % 3 == 2}, {n for n in nums if n % 3 == 0},
        set(range(1, 13)), set(range(13, 25)), set(range(25, 37)),
        set(range(1, 19)), {n for n in nums if n % 2 == 0}, set(REDS), set(nums) - REDS,
        {n for n in nums if n % 2 == 1}, set(range(19, 37)),
    ][k]


def spots(us):
    out = {}
    for v in range(6):
        for u in range(24):
            out[v * 24 + u] = lattice(u, v, us)
    out[144] = {0}
    if us:
        out[145] = {N00}
        out[146] = {0, N00}
    for k in range(12):
        out[147 + k] = outside(k)
    return out


def name_of(i, cov):
    if i >= 147:
        return OUTSIDE[i - 147]
    zero = bool(cov & {0, N00})
    return {1: "STRAIGHT", 2: "SPLIT", 3: "TRIO" if zero else "STREET",
            4: "FIRST FOUR" if zero else "CORNER", 5: "TOP LINE", 6: "SIX LINE"}[len(cov)]


def label(n):
    return "00" if n == N00 else str(n)


def numbers_of(name, cov):
    if name.endswith("COLUMN") or name in ("EVEN", "ODD", "RED", "BLACK"):
        return ""
    order = sorted(cov, key=lambda n: (n != 0, n != N00, n))
    if name in ("STREET", "SIX LINE", "LOW", "HIGH") or name.endswith("DOZEN"):
        return f"{label(order[0])}-{label(order[-1])}"
    return "/".join(label(n) for n in order)


def lines():
    out = []
    for us in (False, True):
        for i, cov in sorted(spots(us).items()):
            name = name_of(i, cov)
            pays = 36 // len(cov) - 1
            covl = ",".join(str(n) for n in sorted(cov))
            out.append(f"{'US' if us else 'EU'} {i} {name}|{numbers_of(name, cov)}|{pays}|{covl}")
    return out


if __name__ == "__main__":
    print("\n".join(lines()))
