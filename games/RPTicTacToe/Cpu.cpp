// The dealer (Cpu.h): a depth-limited search on the 3x3 tables, line
// scoring on the big felts, and his bids at AUCTION.
#pragma GCC optimize("Os")
#include "Cpu.h"
#include <string.h>

namespace cpu {

uint32_t rnd(uint32_t *rng) {
    uint32_t x = *rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *rng = x;
}

static uint8_t lvl, pos, ties, sideNow;
static uint32_t *gen;
static int32_t bestScore;
static Move best;
static bool dull;                   // this move the dealer isn't watching your lines

// ---------------------------------------------------------------------------
// 3x3 tables: search
// ---------------------------------------------------------------------------
struct S9 { uint8_t c[9], q[2][3], qn[2], stock[2][3]; };
static uint16_t fl;

static void load(S9 &s, const Board &b) {
    memcpy(s.c, b.cell, 9);
    memcpy(s.q, b.q, sizeof s.q);
    memcpy(s.qn, b.qn, sizeof s.qn);
    memcpy(s.stock, b.stock, sizeof s.stock);
    fl = b.flags;
}

static bool can(const S9 &s, uint8_t side, uint8_t cell, uint8_t arg) {
    uint8_t c = s.c[cell];
    if (fl & F_GOBBLE) return s.stock[side][arg] && (!c || levelOf(c) < arg);
    return !c;
}

static uint8_t nArgs() { return (fl & F_GOBBLE) ? 3 : ((fl & F_WILD) ? 2 : 1); }

// 1: the mover wins, -1: the mover loses, 0: play on.
static int8_t apply(S9 &s, uint8_t side, uint8_t cell, uint8_t arg) {
    uint8_t who = (uint8_t)(side + 1), l = 0;
    if (fl & F_WILD) who = (uint8_t)(arg + 1);
    if (fl & F_SAME) who = 1;
    if (fl & F_GOBBLE) { l = arg; s.stock[side][l]--; }
    s.c[cell] = (uint8_t)(s.c[cell] | (who << (2 * l)));
    if (fl & F_VANISH) {
        uint8_t *q = s.q[side];
        if (s.qn[side] == 3) { s.c[q[0]] = 0; q[0] = q[1]; q[1] = q[2]; s.qn[side] = 2; }
        q[s.qn[side]++] = cell;
    }
    for (uint8_t i = 0; i < 8; i++) {
        const uint8_t *ln = LINES3[i];
        if (topOf(s.c[ln[0]]) == who && topOf(s.c[ln[1]]) == who && topOf(s.c[ln[2]]) == who)
            return (fl & F_MISERE) ? -1 : 1;
    }
    return 0;
}

static int8_t search(const S9 &s, uint8_t side, uint8_t depth, int8_t alpha, int8_t beta) {
    if (!depth) return 0;
    int8_t bestV = -100;
    uint8_t na = nArgs();
    for (uint8_t cell = 0; cell < 9; cell++) {
        for (uint8_t arg = 0; arg < na; arg++) {
            if (!can(s, side, cell, arg)) continue;
            S9 n = s;
            int8_t r = apply(n, side, cell, arg);
            int8_t v = r ? (int8_t)(r * (20 + depth)) : (int8_t)-search(n, side ^ 1, depth - 1, -beta, -alpha);
            if (v > bestV) bestV = v;
            if (bestV > alpha) alpha = bestV;
            if (alpha >= beta) return bestV;
        }
    }
    return bestV == -100 ? 0 : bestV;
}

bool winsNow(const Board &b, uint8_t side) {
    S9 s;
    load(s, b);
    for (uint8_t cell = 0; cell < 9; cell++)
        for (uint8_t arg = 0; arg < nArgs(); arg++) {
            if (!can(s, side, cell, arg)) continue;
            S9 n = s;
            if (apply(n, side, cell, arg) == 1) return true;
        }
    return false;
}

static void offer(int32_t score, uint8_t cell, uint8_t arg) {
    if (score > bestScore) { bestScore = score; ties = 0; }
    else if (score < bestScore) return;
    if (rnd(gen) % ++ties == 0) { best.cell = cell; best.arg = arg; }   // any of the equals
}

static void think9(const Board &b) {
    static const uint8_t DEPTH[4][3] = {{2, 4, 9}, {2, 3, 5}, {2, 4, 6}, {1, 2, 3}};
    S9 s;
    load(s, b);
    uint8_t kind = (fl & F_GOBBLE) ? 3 : ((fl & F_VANISH) ? 2 : ((fl & F_WILD) ? 1 : 0));
    uint8_t depth = DEPTH[kind][lvl];
    if (fl & (F_COIN | F_AUCTION)) depth = 2;        // nobody knows who moves next
    if (b.left == 9 && depth > 4) depth = 4;         // an empty board: any opening will do
    static const uint8_t BLUNDER[3] = {30, 8, 0};
    bool blunder = rnd(gen) % 100 < (uint8_t)(BLUNDER[lvl] + ((fl & F_BLITZ) ? 15 : 0));
    for (uint8_t cell = 0; cell < 9; cell++)
        for (uint8_t arg = 0; arg < nArgs(); arg++) {
            if (!can(s, b.turn, cell, arg)) continue;
            int32_t v = 0;
            if (!blunder) {
                S9 n = s;
                int8_t r = apply(n, b.turn, cell, arg);
                v = r ? r * (20 + depth) : -search(n, b.turn ^ 1, depth - 1, -100, 100);
            }
            offer(v, cell, arg);
        }
}

// ---------------------------------------------------------------------------
// The big felts: line scoring
// ---------------------------------------------------------------------------
static const int16_t ATTACK[5] = {20000, 300, 30, 4, 1};   // by marks still needed - 1
static const int16_t DEFEND[5] = {5000, 200, 20, 3, 1};

// What the cell is worth to side: every k-window through it that only one
// side has marks in, by how close that side is to filling it.
static int32_t scoreCell(const Board &b, uint8_t cell, uint8_t side, int x0, int y0, int x1, int y1, bool blind) {
    int x = cell % b.w, y = cell / b.w;
    int32_t total = 0;
    bool wrap = (b.flags & F_WRAP) != 0;
    for (uint8_t i = 0; i < 4; i++) {
        const Dir &d = DIRS[i];
        for (int o = 0; o < b.k; o++) {
            int sx = x - o * d.dx, sy = y - o * d.dy;
            int ex = sx + (b.k - 1) * d.dx, ey = sy + (b.k - 1) * d.dy;
            if (!wrap && (sx < x0 || sx >= x1 || ex < x0 || ex >= x1 || sy < y0 || sy >= y1 || ey < y0 || ey >= y1)) continue;
            uint8_t mine = 0, theirs = 0;
            for (int j = 0; j < b.k; j++) {
                int c = (sy + j * d.dy + 8 * b.h) % b.h * b.w + (sx + j * d.dx + 8 * b.w) % b.w;
                uint8_t t = topOf(b.cell[c]);
                if (t == side + 1) mine++;
                else if (t) theirs++;
            }
            if (!theirs) total += ATTACK[b.k - 1 - mine];
            if (!mine) total += blind ? 1 : DEFEND[b.k - 1 - theirs];
        }
    }
    return total;
}

// What taking small board s is worth to who on the board of boards.
static int32_t metaValue(const Board &b, uint8_t s, uint8_t who) {
    int32_t v = 0;
    for (uint8_t i = 0; i < 8; i++) {
        const uint8_t *ln = LINES3[i];
        if (ln[0] != s && ln[1] != s && ln[2] != s) continue;
        uint8_t mine = 0, bad = 0;
        for (uint8_t j = 0; j < 3; j++) {
            if (b.small[ln[j]] == who) mine++;
            else if (b.small[ln[j]]) bad++;
        }
        if (!bad) v += mine == 2 ? 100000 : 200 * (mine + 1);
        if (bad == 2 && !mine && b.small[ln[0]] != 3 && b.small[ln[1]] != 3 && b.small[ln[2]] != 3) v += 2000;   // breaks their line
    }
    return v;
}

static int32_t scoreUltimate(const Board &b, uint8_t cell, uint8_t side) {
    int x0 = cell % 9 / 3 * 3, y0 = cell / 27 * 3;
    int32_t v = scoreCell(b, cell, side, x0, y0, x0 + 3, y0 + 3, dull);
    uint8_t who = (uint8_t)(side + 1), s = smallOf(cell);
    bool takes = v >= ATTACK[0];
    if (takes) v = 3000 + metaValue(b, s, who);
    // Where it sends the other side.
    uint8_t dst = (uint8_t)(cell / 9 % 3 * 3 + cell % 3);
    if (b.small[dst] || (dst == s && takes)) v -= 150;            // anywhere they like
    else {
        int bx = dst % 3 * 3, by = dst / 3 * 3;
        for (int yy = by; yy < by + 3; yy++)
            for (int xx = bx; xx < bx + 3; xx++) {
                uint8_t c = (uint8_t)(yy * 9 + xx);
                if (!b.cell[c] && c != cell && scoreCell(b, c, side ^ 1, bx, by, bx + 3, by + 3, true) >= ATTACK[0]) {
                    v -= 400 + metaValue(b, dst, (uint8_t)(3 - who)) / 2;
                    yy = 99;
                    break;
                }
            }
    }
    return v;
}

// ---------------------------------------------------------------------------
void begin(const Board &b, uint8_t level, uint32_t *rng) {
    lvl = level > 2 ? 2 : level;
    gen = rng;
    pos = 0;
    ties = 0;
    bestScore = INT32_MIN;
    best.cell = NONE; best.arg = 0;
    sideNow = b.turn;
    static const uint8_t DULL[3] = {35, 6, 0};
    dull = rnd(gen) % 100 < DULL[lvl];
}

bool step(const Board &b, Move &out) {
    if (b.n == 9) {
        think9(b);
    } else {
        static const uint16_t JITTER[3] = {160, 24, 3};
        for (uint8_t budget = 12; budget && pos < b.n; pos++) {
            if (!rules::legal(b, pos, 0)) continue;
            budget--;
            int32_t v = (b.flags & F_ULTIMATE) ? scoreUltimate(b, pos, sideNow)
                                               : scoreCell(b, pos, sideNow, 0, 0, b.w, b.h, dull);
            // DROP 4: not the cell that hands them the one above it.
            if ((b.flags & F_GRAVITY) && pos >= b.w && !dull &&
                scoreCell(b, (uint8_t)(pos - b.w), sideNow ^ 1, 0, 0, b.w, b.h, true) >= ATTACK[0]) v -= 4000;
            offer(v + (int32_t)(rnd(gen) % JITTER[lvl]), pos, 0);
        }
        if (pos < b.n) return false;
    }
    out = best;
    return true;
}

uint8_t bid(const Board &b, uint8_t level, uint32_t *rng) {
    uint8_t mine = b.chips[1], yours = b.chips[0];
    // What is certain to win the bid, if I have it.
    uint8_t sure = (uint8_t)(yours + (b.tieTo == 1 ? 0 : 1));
    if (sure > mine) sure = mine;
    bool sees = level > 0 || (rnd(rng) & 3);          // TIPSY misses it one time in four
    if (sees && (winsNow(b, 1) || winsNow(b, 0))) return sure;
    uint8_t top = (uint8_t)(level == 0 ? mine / 2 + 1 : 2 + level);
    uint8_t v = (uint8_t)(rnd(rng) % (top + 1));
    return v > mine ? mine : v;
}

}  // namespace cpu
