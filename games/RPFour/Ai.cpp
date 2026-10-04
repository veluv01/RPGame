// The CPU's search (see Ai.h): an alpha-beta over the bitboards, its stack
// in an array so it can stop after any position and go on next tick.
#pragma GCC optimize("O2")
#include "Ai.h"
#include <rpgame/RamFunc.h>

namespace ai {

using c4::Bits;

constexpr int8_t MAX_PLY = 16;
constexpr int16_t INF = 32000;

// How each opponent looks: the first depth, how much deeper it may go (two
// plies at a time, while the positions last), and how it chooses: a margin
// makes it score every move and take any within that of the best; wild, one
// move in that many is any that does not lose at once.
struct Style { uint8_t depth, maxDepth; uint16_t cap; uint8_t margin, wild; };
static const Style STYLE[LEVELS] = {
    {2, 2, 0, 10, 4},           // ROOKIE: sees a win and a block, and not much further
    {5, 5, 0, 3, 0},            // SHARK: five plies
    {4, 14, 30000, 0, 0},       // BOSS: as deep as 30,000 positions go, the best move every time
};

struct Ply {
    int16_t alpha, beta, best;
    uint8_t moves;              // columns still to try (a bit each)
    uint8_t col;                // the one being tried
};

static Ply st[MAX_PLY + 1];
static c4::Board bd;
static uint8_t rootSide, level, depth, rootMoves, pv, bestCol, pick, reached;
static int8_t ply;
static bool done, wide;
static int16_t rootScore[c4::COLS], result, ret;
static uint32_t visited;
static c4::Rng *rng;

static const uint8_t ORDER[c4::COLS] = {3, 2, 4, 1, 5, 0, 6};

static inline uint8_t columns(Bits cells) {
    uint8_t m = 0;
    for (uint8_t c = 0; c < c4::COLS; c++, cells >>= 7)
        if (cells & 0x7F) m |= (uint8_t)(1 << c);
    return m;
}

// The position with `s` to move, `mine` and `theirs` the cells that would
// make each side a four: threats that are not yet playable, and the middle.
static inline int16_t judge(uint8_t s, Bits mine, Bits theirs) {
    constexpr Bits MID = c4::COLUMN << 21, NEAR = (c4::COLUMN << 14) | (c4::COLUMN << 28);
    int v = 6 * ((int)c4::count(mine) - (int)c4::count(theirs));
    Bits a = bd.side[s], b = bd.side[s ^ 1];
    v += 2 * ((int)c4::count(a & MID) - (int)c4::count(b & MID));
    v += (int)c4::count(a & NEAR) - (int)c4::count(b & NEAR);
    return (int16_t)v;
}

// Arrive at a position `p` plies down. True if it has a value without
// looking further (in ret); else its frame is ready to be searched.
RAMFUNC(aienter) static bool enter(int8_t p, int16_t alpha, int16_t beta) {
    visited++;
    uint8_t s = (uint8_t)((rootSide + p) & 1);
    Bits occ = bd.side[0] | bd.side[1], can = (occ + c4::BOTTOM) & c4::BOARD;
    Bits mine = c4::winning(bd.side[s], occ);
    if (mine & can) { ret = (int16_t)(WIN - p); return true; }
    if (!can) { ret = 0; return true; }                             // the board is full: a draw
    Bits theirs = c4::winning(bd.side[s ^ 1], occ);
    Bits ok = can, forced = theirs & can;
    if (forced) {
        if (forced & (forced - 1)) { ret = (int16_t)(-(WIN - (p + 1))); return true; }   // two to block
        ok = forced;
    }
    ok &= ~(theirs >> 1);                                           // never under a cell they win in
    if (!ok) { ret = (int16_t)(-(WIN - (p + 1))); return true; }
    if (p >= depth) { ret = judge(s, mine, theirs); return true; }
    Ply &f = st[p];
    f.alpha = alpha; f.beta = beta; f.best = -INF;
    f.moves = columns(ok);
    return false;
}

static void rootFrame() {
    Ply &f = st[0];
    f.alpha = -INF; f.beta = INF; f.best = -INF;
    f.moves = rootMoves;
    ply = 0;
}

// The search is over: the weaker opponents now choose among what it found.
static void choose() {
    done = true;
    pick = bestCol;
    const Style &y = STYLE[level];
    if (!wide || winning(result)) return;
    uint8_t cand[c4::COLS], n = 0;
    bool any = y.wild && rng->below(y.wild) == 0;
    for (uint8_t i = 0; i < c4::COLS; i++) {
        uint8_t c = ORDER[i];
        if (!(rootMoves & (1 << c)) || losing(rootScore[c])) continue;
        if (any || rootScore[c] >= result - y.margin) cand[n++] = c;
    }
    if (n) pick = cand[rng->below(n)];
}

void start(const c4::Board &b, uint8_t side, uint8_t lv, c4::Rng &noise) {
    bd = b;
    rootSide = side; level = lv < LEVELS ? lv : (uint8_t)BOSS; rng = &noise;
    visited = 0; done = false; reached = 0;
    pv = 0xFF;
    const Style &y = STYLE[level];
    depth = y.depth;
    wide = y.margin != 0;
    for (auto &r : rootScore) r = -INF;
    // The first move's three questions are asked here: the root always has
    // to come back with a column, however bad things are.
    Bits occ = bd.side[0] | bd.side[1], can = c4::playable(bd);
    Bits mine = c4::winning(bd.side[side], occ) & can;
    Bits theirs = c4::winning(bd.side[side ^ 1], occ);
    bestCol = pick = 3;
    result = 0;
    st[0].col = 3;
    if (!can) { done = true; return; }
    if (mine) { bestCol = pick = c4::columnOf(mine); result = WIN; done = true; return; }
    Bits ok = can;
    if (theirs & can) ok = theirs & can;
    if (ok & ~(theirs >> 1)) ok &= ~(theirs >> 1);
    rootMoves = columns(ok);
    for (uint8_t i = 0; i < c4::COLS; i++)
        if (rootMoves & (1 << ORDER[i])) { bestCol = pick = ORDER[i]; break; }
    rootFrame();
}

// One depth is finished: deeper, or done?
static bool iterated() {
    result = st[0].best;
    reached = depth;
    pv = bestCol;
    const Style &y = STYLE[level];
    uint8_t left = (uint8_t)(c4::CELLS - bd.n);
    if (depth >= y.maxDepth || depth >= left || winning(result) || losing(result) || (y.cap && visited >= y.cap)) {
        choose();
        return true;
    }
    depth = (uint8_t)(depth + 2);
    if (depth > MAX_PLY - 1) depth = MAX_PLY - 1;
    rootFrame();
    return false;
}

RAMFUNC(aistep) bool step(uint16_t budget) {
    if (done) return true;
    uint32_t stop = visited + budget;
    const uint16_t cap = STYLE[level].cap;
    for (;;) {
        Ply &f = st[ply];
        int16_t v;
        if (!f.moves) {
            // Every move here has been tried: its value goes up a ply.
            if (ply == 0) {
                if (iterated()) return true;
                continue;
            }
            v = f.best;
            ply--;
        } else {
            if (visited >= stop) return false;
            // Out of positions part way down a deeper look: the last whole one stands.
            if (cap && visited >= cap && reached) { choose(); return true; }
            uint8_t col = 0xFF;
            if (ply == 0 && pv != 0xFF && (f.moves & (1 << pv))) col = pv;
            else for (uint8_t i = 0; i < c4::COLS; i++)
                if (f.moves & (1 << ORDER[i])) { col = ORDER[i]; break; }
            f.moves &= (uint8_t)~(1 << col);
            f.col = col;
            c4::play(bd, (uint8_t)((rootSide + ply) & 1), col);
            if (!enter((int8_t)(ply + 1), (int16_t)-f.beta, (int16_t)-f.alpha)) { ply++; continue; }
            v = ret;
        }
        // The move st[ply].col is worth -v to the side that made it.
        Ply &g = st[ply];
        v = (int16_t)-v;
        c4::undo(bd, (uint8_t)((rootSide + ply) & 1), g.col);
        if (ply == 0) rootScore[g.col] = v;
        if (v > g.best) {
            g.best = v;
            if (ply == 0) bestCol = g.col;
        }
        if (!(ply == 0 && wide) && v > g.alpha) g.alpha = v;
        if (g.alpha >= g.beta) g.moves = 0;
    }
}

uint8_t chosen() { return pick; }
int16_t score() { return result; }
uint8_t considering() { return done ? pick : st[0].col; }
uint32_t positions() { return visited; }
uint8_t depthReached() { return reached; }

}  // namespace ai
