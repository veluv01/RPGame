// Checkers rules and search. The board is the 32 dark squares in a padded
// row of 46 cells, so that the four diagonals are +4, +5, -4 and -5 from
// any of them and running off the board lands on an OFF cell:
//
//      37  38  39  40        rank 8 (h8 = 40)
//    32  33  34  35
//      28  29  30  31
//    23  24  25  26
//      19  20  21  22
//    14  15  16  17
//      10  11  12  13
//     5   6   7   8          rank 1 (a1 = 5)
#pragma GCC optimize("Os")
#include "Engine.h"

namespace eng {

void (*pollHook)() = nullptr;
static uint32_t rng = 0x9E3779B9u;
uint8_t rand8() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (uint8_t)(rng >> 24);
}
void seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }

#ifdef ENG_CHECKS
#define GUARD() do { if (busy) __builtin_trap(); } while (0)   // host tests: hands off mid-search
#else
#define GUARD() ((void)0)
#endif

static const uint8_t OFF = 0x80;
static const uint8_t FIRST = 5, LAST = 40;
static const int8_t DIRS[4] = {4, 5, -4, -5};       // White's two ways forward, then Black's

struct IStep { uint8_t from, to, cap; };            // cells; cap 0: none

static uint8_t bd[46];
static uint8_t side;                                // 0 or BLACK: who moves
static uint8_t chain;                               // the cell that must jump on, 0: none
static uint8_t ruleBits = R_FORCED;
static uint8_t noProg;                              // moves since a jump or a man's move
static uint16_t plyN;
// Positions since the last jump or man's move (hashes), for repetition.
static const uint8_t RING = 40;
static uint32_t ring[RING];
static uint8_t ringN, ringAt;

// Steps: the game's at the bottom, the search's lists stacked above.
#ifndef ENG_POOL
#define ENG_POOL 224
#endif
static IStep pool[ENG_POOL];

static bool busy;

static inline uint8_t cellOf(uint8_t i) { return (uint8_t)(5 + i + (((i >> 2) + 1) >> 1)); }   // dark square 0..31
static inline uint8_t idxOf(uint8_t c) { return (uint8_t)(c - 5 - c / 9); }
static uint8_t sqOf(uint8_t c) {
    uint8_t i = idxOf(c), r = (uint8_t)(i >> 2);
    return (uint8_t)(r * 8 + (i & 3) * 2 + (r & 1));
}
static uint8_t cellOfSq(uint8_t s) {                // 0: a light square
    uint8_t r = (uint8_t)(s >> 3), f = (uint8_t)(s & 7);
    if (s > 63 || ((r ^ f) & 1)) return 0;
    return cellOf((uint8_t)(r * 4 + (f >> 1)));
}
static inline bool isPiece(uint8_t p) { return p && !(p & OFF); }

// ---------------------------------------------------------------------------
// Steps
// ---------------------------------------------------------------------------
static uint8_t genCaps(uint8_t c, IStep *out, uint8_t room) {
    uint8_t p = bd[c], n = 0;
    bool king = (p & TYPE) == KING, fly = king && (ruleBits & R_FLYING);
    uint8_t d0 = 0, d1 = 4;
    if (!king && !(ruleBits & R_BACKJUMP)) { if (p & BLACK) d0 = 2; else d1 = 2; }
    for (uint8_t d = d0; d < d1; d++) {
        int8_t dir = DIRS[d];
        uint8_t x = (uint8_t)(c + dir);
        if (fly) while (bd[x] == EMPTY) x = (uint8_t)(x + dir);
        uint8_t v = bd[x];
        if (!isPiece(v) || !((v ^ p) & BLACK)) continue;
        for (uint8_t l = (uint8_t)(x + dir); bd[l] == EMPTY; l = (uint8_t)(l + dir)) {
            if (n < room) { out[n].from = c; out[n].to = l; out[n].cap = x; n++; }
            if (!fly) break;
        }
    }
    return n;
}

static uint8_t genQuiet(uint8_t c, IStep *out, uint8_t room) {
    uint8_t p = bd[c], n = 0;
    bool king = (p & TYPE) == KING, fly = king && (ruleBits & R_FLYING);
    uint8_t d0 = 0, d1 = 4;
    if (!king) { if (p & BLACK) d0 = 2; else d1 = 2; }
    for (uint8_t d = d0; d < d1; d++) {
        int8_t dir = DIRS[d];
        for (uint8_t x = (uint8_t)(c + dir); bd[x] == EMPTY; x = (uint8_t)(x + dir)) {
            if (n < room) { out[n].from = c; out[n].to = x; out[n].cap = 0; n++; }
            if (!fly) break;
        }
    }
    return n;
}

// Jumps first (the only steps when one is compulsory, or mid-chain), then
// the slides.
static uint8_t gen(IStep *out, uint8_t room, bool jumpsOnly) {
    if (chain) return genCaps(chain, out, room);
    uint8_t n = 0;
    for (uint8_t c = FIRST; c <= LAST; c++)
        if (isPiece(bd[c]) && (bd[c] & BLACK) == side) n = (uint8_t)(n + genCaps(c, out + n, (uint8_t)(room - n)));
    if (jumpsOnly || (n && (ruleBits & R_FORCED))) return n;
    for (uint8_t c = FIRST; c <= LAST; c++)
        if (isPiece(bd[c]) && (bd[c] & BLACK) == side) n = (uint8_t)(n + genQuiet(c, out + n, (uint8_t)(room - n)));
    return n;
}

struct Undo { uint8_t moved, captured, chain, noProg, side; };

// True once the move is complete: the turn passes. A man reaching the far
// row is crowned and stops there.
static bool apply(const IStep &s, Undo &u) {
    uint8_t p = bd[s.from];
    u.moved = p; u.chain = chain; u.noProg = noProg; u.side = side; u.captured = 0;
    bd[s.from] = EMPTY;
    bool crowned = false;
    if ((p & TYPE) == MAN && ((p & BLACK) ? s.to <= 8 : s.to >= 37)) {
        p = (uint8_t)(KING | (p & BLACK));
        crowned = true;
    }
    bd[s.to] = p;
    if (s.cap) {
        u.captured = bd[s.cap];
        bd[s.cap] = EMPTY;
        IStep t;
        if (!crowned && genCaps(s.to, &t, 1)) { chain = s.to; return false; }
    }
    chain = 0;
    side ^= BLACK;
    noProg = (s.cap || (u.moved & TYPE) == MAN) ? 0 : (uint8_t)(noProg + 1);
    return true;
}

static void unapply(const IStep &s, const Undo &u) {
    bd[s.to] = EMPTY;
    bd[s.from] = u.moved;
    if (s.cap) bd[s.cap] = u.captured;
    chain = u.chain; noProg = u.noProg; side = u.side;
}

static uint32_t hashNow() {
    uint32_t h = 2166136261u ^ side;
    for (uint8_t c = FIRST; c <= LAST; c++) h = (h ^ bd[c]) * 16777619u;
    return h;
}

static uint8_t seen(uint32_t h) {
    uint8_t k = 0;
    for (uint8_t i = 0; i < ringN; i++) k = (uint8_t)(k + (ring[i] == h));
    return k;
}

static void remember() {
    if (!noProg) ringN = ringAt = 0;
    ring[ringAt] = hashNow();
    ringAt = (uint8_t)((ringAt + 1) % RING);
    if (ringN < RING) ringN++;
}

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------
static void clearBoard() {
    for (uint8_t c = 0; c < 46; c++) bd[c] = OFF;
    for (uint8_t i = 0; i < 32; i++) bd[cellOf(i)] = EMPTY;
    side = 0; chain = 0; noProg = 0; plyN = 0;
    ringN = ringAt = 0;
}

void newGame(uint8_t r) {
    GUARD();
    ruleBits = r;
    clearBoard();
    for (uint8_t i = 0; i < 12; i++) {
        bd[cellOf(i)] = MAN;
        bd[cellOf((uint8_t)(20 + i))] = MAN | BLACK;
    }
    remember();
}

uint8_t rules() { return ruleBits; }

void snapshot(Snap &s) {
    GUARD();
    s.white = s.black = s.kings = 0;
    for (uint8_t i = 0; i < 32; i++) {
        uint8_t p = bd[cellOf(i)];
        if (!p) continue;
        uint32_t bit = 1u << i;
        if (p & BLACK) s.black |= bit; else s.white |= bit;
        if ((p & TYPE) == KING) s.kings |= bit;
    }
    s.blackToMove = side != 0;
    s.noProgress = noProg;
    s.ply = plyN;
}

void restore(const Snap &s, uint8_t r) {
    GUARD();
    ruleBits = r;
    clearBoard();
    for (uint8_t i = 0; i < 32; i++) {
        uint32_t bit = 1u << i;
        if (!((s.white | s.black) & bit)) continue;
        bd[cellOf(i)] = (uint8_t)(((s.kings & bit) ? KING : MAN) | ((s.black & bit) ? BLACK : 0));
    }
    side = s.blackToMove ? BLACK : 0;
    noProg = s.noProgress;
    plyN = s.ply;
    ring[0] = hashNow();
    ringN = ringAt = 1;
}

#if defined(CHSIM) || defined(CHTEST)
void setup(const char *cells, bool black, uint8_t r) {
    GUARD();
    ruleBits = r;
    clearBoard();
    for (uint8_t i = 0; i < 32 && cells[i]; i++) {
        char ch = cells[i];
        uint8_t p = ch == 'w' ? MAN : ch == 'W' ? KING : ch == 'b' ? (MAN | BLACK) : ch == 'B' ? (KING | BLACK) : EMPTY;
        bd[cellOf(i)] = p;
    }
    side = black ? BLACK : 0;
    plyN = 20;
    remember();
}
#endif

static uint8_t give(const IStep *m, uint8_t n, Step *out, uint8_t max, uint8_t fromCell) {
    uint8_t k = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (fromCell && m[i].from != fromCell) continue;
        if (k < max) {
            out[k].from = sqOf(m[i].from);
            out[k].to = sqOf(m[i].to);
            out[k].cap = m[i].cap ? sqOf(m[i].cap) : NONE;
        }
        k++;
    }
    return k < max ? k : max;
}

uint8_t steps(Step *out, uint8_t max) {
    GUARD();
    return give(pool, gen(pool, ENG_POOL, false), out, max, 0);
}

uint8_t stepsFrom(uint8_t from, Step *out, uint8_t max) {
    GUARD();
    uint8_t c = cellOfSq(from);
    return c ? give(pool, gen(pool, ENG_POOL, false), out, max, c) : 0;
}

uint8_t stepCount() { GUARD(); return gen(pool, ENG_POOL, false); }

int16_t indexOf(uint8_t from, uint8_t to) {
    GUARD();
    uint8_t n = gen(pool, ENG_POOL, false), f = cellOfSq(from), t = cellOfSq(to);
    for (uint8_t i = 0; i < n; i++) if (pool[i].from == f && pool[i].to == t) return i;
    return -1;
}

bool stepAt(uint8_t index, Step &out) {
    GUARD();
    uint8_t n = gen(pool, ENG_POOL, false);
    if (index >= n) return false;
    give(pool + index, 1, &out, 1, 0);
    return true;
}

bool play(uint8_t index) {
    GUARD();
    uint8_t n = gen(pool, ENG_POOL, false);
    if (index >= n) return true;
    Undo u;
    if (!apply(pool[index], u)) return false;
    plyN++;
    remember();
    return true;
}

uint8_t chainSq() { return chain ? sqOf(chain) : NONE; }
bool canJump() { GUARD(); return gen(pool, ENG_POOL, true) != 0; }

uint8_t pieceAt(uint8_t s) {
    uint8_t c = cellOfSq(s);
    return c ? bd[c] : (uint8_t)EMPTY;
}

uint8_t count(bool black) {
    uint8_t n = 0;
    for (uint8_t c = FIRST; c <= LAST; c++) n = (uint8_t)(n + (isPiece(bd[c]) && ((bd[c] & BLACK) != 0) == black));
    return n;
}

bool blackToMove() { return side != 0; }
uint16_t ply() { return plyN; }

Status status() {
    GUARD();
    if (chain) return NORMAL;
    if (!gen(pool, ENG_POOL, false)) return count(side != 0) ? LOST_BLOCKED : LOST_NO_PIECES;
    if (noProg >= 80) return DRAW_40;
    if (seen(hashNow()) >= 3) return DRAW_REPETITION;
    return NORMAL;
}

// ---------------------------------------------------------------------------
// The CPU: alpha-beta over steps (a hop that leaves a jump to make keeps the
// turn - and the depth), iterative deepening inside a node budget. At the
// horizon only jumps are searched on, so no position is judged mid-exchange.
// ---------------------------------------------------------------------------
static const int16_t INF = 30000, MATE = 20000;
static const uint8_t MAX_PLY = 18, MAX_DEPTH = 14, ROOT_MAX = 48;

static uint32_t nodeN, maxNodes;
static uint8_t stop;                                // 1: out of nodes, 2: aborted
static volatile uint8_t rootCur;
static uint8_t rootN;
static int16_t rootScore[ROOT_MAX], score_;

// The side to move's view. Men: 100, worth more the further up the board
// and holding the back row; kings prefer the middle; ahead, trade down.
static int16_t eval() {
    int16_t s = 0;
    int8_t n = 0;
    int16_t kingVal = (ruleBits & R_FLYING) ? 320 : 160;
    for (uint8_t c = FIRST; c <= LAST; c++) {
        uint8_t p = bd[c];
        if (!isPiece(p)) continue;
        uint8_t i = idxOf(c);
        int8_t r = (int8_t)(i >> 2), f = (int8_t)((i & 3) * 2 + (r & 1));
        int16_t v;
        if ((p & TYPE) == MAN) {
            int8_t adv = (p & BLACK) ? (int8_t)(7 - r) : r;
            v = (int16_t)(100 + adv * 3 + (adv == 0 ? 10 : 0) + ((f >= 2 && f <= 5) ? 2 : 0));
        } else {
            int8_t df = (int8_t)(2 * f - 7), dr = (int8_t)(2 * r - 7);
            if (df < 0) df = (int8_t)-df;
            if (dr < 0) dr = (int8_t)-dr;
            v = (int16_t)(kingVal + 6 - (df + dr) / 2);
        }
        if (p & BLACK) s = (int16_t)(s - v); else s = (int16_t)(s + v);
        n++;
    }
    if (s > 50) s = (int16_t)(s + (24 - n) * 4);
    else if (s < -50) s = (int16_t)(s - (24 - n) * 4);
    return side ? (int16_t)-s : s;
}

static int16_t search(int8_t depth, int16_t alpha, int16_t beta, uint8_t ply, uint8_t base) {
    if (!(++nodeN & (POLL_NODES - 1)) && pollHook) pollHook();
    if (stop) return 0;
    if (nodeN >= maxNodes) { stop = 1; return 0; }
    if (ply >= MAX_PLY || ENG_POOL - base < 16) return eval();
    bool horizon = depth <= 0 && !chain;
    IStep *m = pool + base;
    uint8_t n = gen(m, (uint8_t)(ENG_POOL - base), horizon);
    int16_t best = -INF;
    if (horizon) {
        if (!n) return eval();
        if (!(ruleBits & R_FORCED)) {               // free to decline the jump
            best = eval();
            if (best >= beta) return best;
            if (best > alpha) alpha = best;
        }
    } else if (!n) {
        return (int16_t)(ply - MATE);
    }
    for (uint8_t i = 0; i < n; i++) {
        Undo u;
        bool done = apply(m[i], u);
        int16_t v = done ? (int16_t)-search((int8_t)(depth - 1), (int16_t)-beta, (int16_t)-alpha, (uint8_t)(ply + 1), (uint8_t)(base + n))
                         : search(depth, alpha, beta, (uint8_t)(ply + 1), (uint8_t)(base + n));
        unapply(m[i], u);
        if (stop) return 0;
        if (v > best) {
            best = v;
            if (v > alpha) { alpha = v; if (v >= beta) break; }
        }
    }
    return best;
}

int16_t think(const Level &lv) {
    GUARD();
    rootN = gen(pool, ROOT_MAX, false);
    nodeN = 0; stop = 0; score_ = 0;
    if (!rootN) return -1;
    if (rootN == 1) return 0;
    maxNodes = lv.nodes;
    uint8_t ord[ROOT_MAX];
    for (uint8_t i = 0; i < rootN; i++) { ord[i] = i; rootScore[i] = 0; }
    busy = true;
    bool have = false;
    for (uint8_t depth = 1; depth <= MAX_DEPTH && !stop; depth++) {
        int16_t tmp[ROOT_MAX], best = -INF;
        for (uint8_t k = 0; k < rootN; k++) {
            uint8_t i = ord[k];
            rootCur = i;
            // Everything within the margin of the best gets its true score.
            int16_t lo = best == -INF ? (int16_t)-INF : (int16_t)(best - lv.margin - 1);
            Undo u;
            bool done = apply(pool[i], u);
            int16_t v;
            if (done) {
                bool rep = seen(hashNow()) != 0;
                v = (int16_t)-search((int8_t)(depth - 1), (int16_t)-INF, (int16_t)-lo, 1, rootN);
                if (rep) v = (int16_t)(v / 4);      // back where we were: nearer a draw
            } else {
                v = search((int8_t)depth, lo, INF, 1, rootN);
            }
            unapply(pool[i], u);
            if (stop) break;
            tmp[i] = v;
            if (v > best) best = v;
        }
        if (stop) break;
        have = true;
        score_ = best;
        for (uint8_t i = 0; i < rootN; i++) rootScore[i] = tmp[i];
        for (uint8_t a = 1; a < rootN; a++) {       // best first, for the next pass
            uint8_t x = ord[a], b = a;
            while (b && rootScore[ord[b - 1]] < rootScore[x]) { ord[b] = ord[b - 1]; b--; }
            ord[b] = x;
        }
        if (best > MATE - 100 || best < 100 - MATE) break;
    }
    busy = false;
    if (stop == 2) return -1;
    if (!have) return ord[0];
    // Any step within the margin of the best, evenly: the lower levels play
    // plausible but beatable checkers.
    int16_t floor = (int16_t)(score_ - lv.margin);
    uint8_t ok = 0;
    for (uint8_t i = 0; i < rootN; i++) ok = (uint8_t)(ok + (rootScore[i] >= floor));
    uint8_t pick = (uint8_t)(((uint16_t)rand8() * ok) >> 8);
    for (uint8_t i = 0; i < rootN; i++)
        if (rootScore[i] >= floor && !pick--) return i;
    return ord[0];
}

int16_t benchThink(const Level &lv) {
    void (*hook)() = pollHook;
    pollHook = nullptr;
    int16_t r = think(lv);
    pollHook = hook;
    return r;
}

void abort() { stop = 2; }
bool aborted() { return stop == 2; }
int16_t lastScore() { return score_; }
uint32_t nodes() { return nodeN; }
bool thinking() { return busy; }
uint8_t rootFrom() { return busy ? sqOf(pool[rootCur].from) : NONE; }

}  // namespace eng
