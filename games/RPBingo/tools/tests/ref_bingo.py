"""An independent model of the round set-up in Bingo.cpp: the
generator, the draw, the cards and the call a rival wins on. rpgame test
compares its lines with the game's `--dump`."""

M = 0xFFFFFFFF


class Rng:
    def __init__(self, s):
        self.s = s or 0x9E3779B9

    def next(self):
        s = self.s
        s ^= (s << 13) & M
        s ^= s >> 17
        s ^= (s << 5) & M
        self.s = s
        return s


def deal(rng):
    card = [0] * 25
    for c in range(5):
        pool = [c * 15 + 1 + i for i in range(15)]
        for r in range(5):
            j = r + rng.next() % (15 - r)
            pool[j], pool[r] = pool[r], pool[j]
            card[r * 5 + c] = pool[r]
    card[12] = 0
    return card


def lines():
    out = [[r * 5 + c for c in range(5)] for r in range(5)]
    out += [[r * 5 + c for r in range(5)] for c in range(5)]
    out += [[i * 6 for i in range(5)], [4 + i * 4 for i in range(5)]]
    return out


def first_line(card, pos):
    return min(max(pos[card[i]] for i in ln) for ln in lines())


def round_(seed, n, rivals):
    rng = Rng(seed)
    balls = list(range(1, 76))
    for i in range(74, 0, -1):
        j = rng.next() % (i + 1)
        balls[i], balls[j] = balls[j], balls[i]
    cards = [deal(rng) for _ in range(n)]
    pos = [0] * 76
    for i, b in enumerate(balls):
        pos[b] = i + 1
    hall, table = 75, 2
    for r in range(rivals):
        at = first_line(deal(rng), pos)
        if at < hall:
            hall, table = at, r + 2
    mine = min(first_line(c, pos) for c in cards)
    return balls, cards, hall, table, mine


def dump_lines():
    out = []
    for seed in range(1, 41):
        for n, rivals in ((1, 8), (4, 20), (9, 40)):
            balls, cards, hall, table, mine = round_(seed * 2654435761 & M, n, rivals)
            out.append(f"{seed} {n} {rivals} hall={hall} table={table} mine={mine} "
                       f"balls={','.join(map(str, balls[:12]))} "
                       f"last={','.join(map(str, cards[-1]))}")
    return out
