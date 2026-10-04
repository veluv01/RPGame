// The rules (see Rules.h): a checker's step, a turn played a step at a time,
// where a checker may go, every play of a roll, and the dice.
#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Rules.h"
#include <rpgame/RamFunc.h>

// The CPU calls the step functions and the enumerator tens of thousands of
// times a move, so they run from SRAM (rpgame/RamFunc.h): about twice as fast as
// from flash on this chip.

namespace bg {

void reset(Board &b) {
    memset(&b, 0, sizeof b);
    for (uint8_t s = 0; s < 2; s++) {
        b.n[s][24] = 2; b.n[s][13] = 5; b.n[s][8] = 3; b.n[s][6] = 5;
        b.far[s] = 10;
    }
}

void recount(Board &b) {
    for (uint8_t s = 0; s < 2; s++) {
        uint8_t f = 0;
        for (uint8_t p = 7; p <= BAR; p++) f += b.n[s][p];
        b.far[s] = f;
    }
}

bool valid(const Board &b) {
    for (uint8_t s = 0; s < 2; s++) {
        uint8_t all = 0, f = 0;
        for (uint8_t p = 0; p <= BAR; p++) { all += b.n[s][p]; if (p > 6) f += b.n[s][p]; }
        if (all != CHECKERS || f != b.far[s]) return false;
    }
    for (uint8_t p = 1; p <= 24; p++) if (b.n[0][p] && b.n[1][25 - p]) return false;
    return true;
}

// The other side has exactly one checker where `side` counts point p.
static inline bool blot(const Board &b, uint8_t side, uint8_t p) { return b.n[side ^ 1][25 - p] == 1; }

RAMFUNC(bgcanstep) bool canStep(const Board &b, uint8_t s, uint8_t from, uint8_t die) {
    if (!b.n[s][from]) return false;
    if (from != BAR && b.n[s][BAR]) return false;           // checkers on the bar come in first
    if (from > die) return b.n[s ^ 1][25 - (from - die)] < 2;
    // Bearing off: everything home, and a die bigger than the point only
    // from the highest point occupied.
    if (b.far[s]) return false;
    if (from == die) return true;
    for (uint8_t p = from + 1; p <= 6; p++) if (b.n[s][p]) return false;
    return true;
}

RAMFUNC(bgdostep) bool doStep(Board &b, uint8_t s, uint8_t from, uint8_t die) {
    uint8_t to = landing(from, die);
    b.n[s][from]--;
    b.n[s][to]++;
    if (from > 6 && to <= 6) b.far[s]--;
    if (!to) return false;
    uint8_t o = s ^ 1, q = 25 - to;
    if (!b.n[o][q]) return false;
    b.n[o][q] = 0;                                          // a blot: to the bar
    b.n[o][BAR]++;
    if (q <= 6) b.far[o]++;
    return true;
}

RAMFUNC(bgundostep) void undoStep(Board &b, uint8_t s, uint8_t from, uint8_t die, bool hit) {
    uint8_t to = landing(from, die);
    b.n[s][to]--;
    b.n[s][from]++;
    if (from > 6 && to <= 6) b.far[s]++;
    if (hit) {
        uint8_t o = s ^ 1, q = 25 - to;
        b.n[o][BAR]--;
        b.n[o][q] = 1;
        if (q <= 6) b.far[o]--;
    }
}

// The most of d[0..n) playable in this order. Equal dice (`same`) are tried
// from the highest point down, never climbing back: any play of equal dice
// can be put in that order, so nothing is missed and nothing counted twice.
static uint8_t playable(Board &b, uint8_t s, const uint8_t *d, uint8_t n, uint8_t top, bool same) {
    uint8_t best = 0;
    if (!n) return 0;
    for (uint8_t f = top; f; f--) {
        if (!canStep(b, s, f, d[0])) continue;
        bool h = doStep(b, s, f, d[0]);
        uint8_t r = 1 + playable(b, s, d + 1, n - 1, same ? f : BAR, same);
        undoStep(b, s, f, d[0], h);
        if (r > best) { best = r; if (best == n) break; }
    }
    return best;
}

uint8_t maxPlayable(Board &b, uint8_t s, const Dice &dice) {
    if (dice.n != 2 || dice.d[0] == dice.d[1]) return playable(b, s, dice.d, dice.n, BAR, true);
    uint8_t r = playable(b, s, dice.d, 2, BAR, false);
    if (r < 2) {
        uint8_t swapped[2] = {dice.d[1], dice.d[0]};
        uint8_t q = playable(b, s, swapped, 2, BAR, false);
        if (q > r) r = q;
    }
    return r;
}

static bool anyStep(const Board &b, uint8_t s, uint8_t die) {
    for (uint8_t f = BAR; f; f--) if (canStep(b, s, f, die)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// A turn, a step at a time
// ---------------------------------------------------------------------------
void beginTurn(Turn &t, Board &b, uint8_t s, uint8_t d1, uint8_t d2) {
    memset(&t, 0, sizeof t);
    t.dice = roll2(d1, d2);
    t.need = maxPlayable(b, s, t.dice);
    if (d1 != d2 && t.need == 1) {
        uint8_t hi = d1 > d2 ? d1 : d2, lo = d1 > d2 ? d2 : d1;
        t.forced = anyStep(b, s, hi) ? hi : lo;
    }
}

static void dropDie(Dice &d, uint8_t die) {
    for (uint8_t i = 0; i < d.n; i++)
        if (d.d[i] == die) {
            for (; i + 1 < d.n; i++) d.d[i] = d.d[i + 1];
            d.n--;
            return;
        }
}

bool stepAllowed(Turn &t, Board &b, uint8_t s, uint8_t from, uint8_t die) {
    bool have = false;
    for (uint8_t i = 0; i < t.dice.n; i++) have |= t.dice.d[i] == die;
    if (!have || !canStep(b, s, from, die)) return false;
    // The last checker may always go off, even past a way to use both dice.
    if (b.n[s][OFF] == CHECKERS - 1 && from <= die) return true;
    if (!t.need || (t.forced && die != t.forced)) return false;
    Dice rest = t.dice;
    dropDie(rest, die);
    bool h = doStep(b, s, from, die);
    bool ok = maxPlayable(b, s, rest) == t.need - 1;
    undoStep(b, s, from, die, h);
    return ok;
}

bool playStep(Turn &t, Board &b, uint8_t s, uint8_t from, uint8_t die) {
    bool h = doStep(b, s, from, die);
    uint8_t k = t.steps++;
    t.from[k] = from; t.die[k] = die; t.hit[k] = h;
    t.needWas[k] = t.need;
    dropDie(t.dice, die);
    if (t.need) t.need--;
    if (b.n[s][OFF] == CHECKERS) t.need = 0;
    return h;
}

bool takeBack(Turn &t, Board &b, uint8_t s) {
    if (!t.steps) return false;
    uint8_t k = --t.steps;
    undoStep(b, s, t.from[k], t.die[k], t.hit[k] != 0);
    t.dice.d[t.dice.n++] = t.die[k];
    t.need = t.needWas[k];
    return true;
}

// Follows `die[0..n)` from `from`, each step allowed in turn; fills the target.
static bool route(Turn &t, Board &b, uint8_t s, uint8_t from, const uint8_t *die, uint8_t n, Target &out) {
    uint8_t done = 0, at = from;
    bool ok = true;
    out.n = n; out.hits = 0;
    for (; done < n; done++) {
        // Only the last step may leave the board.
        if (!stepAllowed(t, b, s, at, die[done]) || (done + 1 < n && at <= die[done])) { ok = false; break; }
        out.die[done] = die[done];
        if (playStep(t, b, s, at, die[done])) out.hits |= (uint8_t)(1 << done);
        at = landing(at, die[done]);
    }
    out.to = at;
    while (done--) takeBack(t, b, s);
    return ok;
}

uint8_t targets(Turn &t, Board &b, uint8_t s, uint8_t from, Target *out) {
    uint8_t n = 0;
    if (!t.dice.n) return 0;
    uint8_t a = t.dice.d[0], c = t.dice.n > 1 ? t.dice.d[1] : a;
    if (t.dice.n > 1 && a == c) {
        // Equal dice: one, two, three, four of them in a row.
        uint8_t d[4] = {a, a, a, a};
        for (uint8_t k = 1; k <= t.dice.n; k++) {
            if (!route(t, b, s, from, d, k, out[n])) break;
            n++;
            if (out[n - 1].to == OFF) break;
        }
        return n;
    }
    if (a < c) { uint8_t x = a; a = c; c = x; }             // a: the bigger
    // Single dice. Both may bear the checker off: then the smaller that does.
    Target one;
    bool offDone = false;
    for (uint8_t k = 0; k < 2; k++) {
        uint8_t d = k ? a : c;
        if (k && (t.dice.n < 2 || a == c)) break;
        if (!route(t, b, s, from, &d, 1, one)) continue;
        if (one.to == OFF) { if (offDone) continue; offDone = true; }
        out[n++] = one;
    }
    if (t.dice.n < 2) return n;
    // Both dice with this checker: the big die first, unless only the other
    // order hits on the way (or only the other order can be played).
    uint8_t ab[2] = {a, c}, ba[2] = {c, a};
    Target x, y;
    bool okx = route(t, b, s, from, ab, 2, x), oky = route(t, b, s, from, ba, 2, y);
    if (okx || oky) {
        const Target &pick = !okx ? y : (!oky ? x : ((y.hits & 1) && !(x.hits & 1) ? y : x));
        if (!(pick.to == OFF && offDone)) out[n++] = pick;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Every play of a roll
// ---------------------------------------------------------------------------
static inline __attribute__((always_inline)) void pop(Plays &g, Board &b) {
    uint8_t k = --g.depth;
    undoStep(b, g.side, g.from[k], g.die[k], g.hit[k] != 0);
    g.from[k]--;                                            // go on below it
}

// `d1` from x, then `d2` with the same checker: legal, and it hits nothing
// on the way.
RAMFUNC(bgquiet) static bool quietChain(Board &b, uint8_t s, uint8_t x, uint8_t d1, uint8_t d2) {
    if (x <= d1 || !canStep(b, s, x, d1) || blot(b, s, (uint8_t)(x - d1))) return false;
    doStep(b, s, x, d1);
    bool ok = canStep(b, s, (uint8_t)(x - d1), d2);
    undoStep(b, s, x, d1, false);
    return ok;
}

// With two different dice a > b, a play is "a from p and b from q", and the
// same position can come up more than once: the two steps in either order,
// one checker making both steps by either route, two checkers borne off
// either way round. This says whether the play about to be completed by a
// second step from f repeats one that is (or was) given another way.
RAMFUNC(bgrepeated) static bool repeated(Plays &g, Board &b, uint8_t f) {
    uint8_t s = g.side, a = g.a, c = g.b, first = g.from[0];
    bool dup = false;
    undoStep(b, s, first, g.die[0], g.hit[0] != 0);         // as the play began
    if (g.pass == 0) {
        // a from p, then b from p + b into the gap: the same as the checker on
        // p + b making both steps, a then b (if that hits nothing on the way).
        if (f == first + c) dup = quietChain(b, s, f, a, c);
    } else {
        uint8_t q = first, p = f;                           // b from q was played; a from p now
        if (canStep(b, s, p, a)) {                          // the same steps, a first: given already
            bool h = doStep(b, s, p, a);
            dup = canStep(b, s, q, c);
            undoStep(b, s, p, a, h);
        }
        // One checker, b then a: the same as a then b (if neither hits on the way).
        if (!dup && p + c == q && !blot(b, s, p)) dup = quietChain(b, s, q, a, c);
        // Both borne off, the lower with the bigger die: given the other way round.
        if (!dup && p < q && q <= c) dup = true;
    }
    doStep(b, s, first, g.die[0]);
    return dup;
}

void begin(Plays &g, Board &b, uint8_t side, uint8_t d1, uint8_t d2) {
    memset(&g, 0, sizeof g);
    g.side = side;
    g.a = d1 > d2 ? d1 : d2;
    g.b = d1 > d2 ? d2 : d1;
    g.dbl = d1 == d2;
    g.need = maxPlayable(b, side, roll2(g.a, g.b));
    g.die[0] = g.die[2] = g.die[3] = g.a;
    g.die[1] = g.dbl ? g.a : g.b;
    // Only one of two dice playable: the higher if it can be, else the lower.
    if (!g.dbl && g.need == 1 && !anyStep(b, side, g.a)) g.die[0] = g.b;
    g.from[0] = BAR;
}

RAMFUNC(bgnext) bool next(Plays &g, Board &b) {
    if (g.done) return false;
    if (!g.need) {                                          // no move: the one play is the pass
        g.done = g.yielded;
        g.yielded = true;
        return !g.done;
    }
    if (g.yielded) { g.yielded = false; pop(g, b); }
    for (;;) {
        uint8_t k = g.depth, die = g.die[k], f = g.from[k];
        for (; f; f--)
            if (canStep(b, g.side, f, die) && !(k == 1 && !g.dbl && repeated(g, b, f))) break;
        if (!f) {
            if (k) { pop(g, b); continue; }
            if (!g.dbl && g.need == 2 && !g.pass) {         // now the smaller die first
                g.pass = 1;
                g.die[0] = g.b; g.die[1] = g.a;
                g.from[0] = BAR;
                continue;
            }
            g.done = true;
            return false;
        }
        g.from[k] = f;
        g.hit[k] = doStep(b, g.side, f, die);
        g.depth = (uint8_t)(k + 1);
        if (g.depth == g.need) { g.yielded = true; return true; }
        g.from[g.depth] = g.dbl ? f : BAR;                  // equal dice: never back up the board
    }
}

// ---------------------------------------------------------------------------
// Counting
// ---------------------------------------------------------------------------
uint8_t result(const Board &b, uint8_t &winner) {
    for (uint8_t s = 0; s < 2; s++) {
        if (b.n[s][OFF] != CHECKERS) continue;
        uint8_t o = s ^ 1;
        winner = s;
        if (b.n[o][OFF]) return 1;
        if (b.n[o][BAR]) return 3;
        for (uint8_t p = 19; p <= 24; p++) if (b.n[o][p]) return 3;
        return 2;
    }
    return 0;
}

uint16_t pips(const Board &b, uint8_t s) {
    uint16_t sum = 0;
    for (uint8_t p = 1; p <= BAR; p++) sum += (uint16_t)(p * b.n[s][p]);
    return sum;
}

bool contact(const Board &b) {
    uint8_t hi[2] = {0, 0};
    for (uint8_t s = 0; s < 2; s++)
        for (uint8_t p = BAR; p; p--) if (b.n[s][p]) { hi[s] = p; break; }
    return hi[0] + hi[1] >= 25;                             // the rearmost checkers have not passed
}

// ---------------------------------------------------------------------------
// Dice
// ---------------------------------------------------------------------------
static const uint64_t PCG_MUL = 6364136223846793005ull, PCG_INC = 1442695040888963407ull;

uint32_t Rng::next() {
    uint64_t old = state;
    state = old * PCG_MUL + PCG_INC;
    uint32_t x = (uint32_t)(((old >> 18) ^ old) >> 27), rot = (uint32_t)(old >> 59);
    return (x >> rot) | (x << ((32 - rot) & 31));
}

void Rng::seed(uint32_t s, uint32_t stream) {
    state = ((uint64_t)stream << 32 | s) * PCG_MUL + PCG_INC;
    next(); next();
}

uint8_t Rng::die() {
    uint32_t r;
    do r = next() >> 29; while (r >= 6);                    // 0..7: throw 6 and 7 away, no bias
    return (uint8_t)(r + 1);
}

}  // namespace bg
