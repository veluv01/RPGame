#pragma GCC optimize("Os")   // cold code: size over speed
// The rules (MahjongBoard.h): what is free, the deal (a full table taken
// apart backwards), play and chips, and the record a save replays.
#include <string.h>
#include "MahjongBoard.h"
#include "src/game/Layouts.h"

namespace board {

Pos pos[MAX_TILES];
uint8_t face[MAX_TILES];
uint8_t count, left, layout;
int32_t chips;
uint8_t streak;
uint16_t streakT;
uint32_t ticks;
uint16_t bonus;
uint8_t mark = NONE;
#if defined(CHSIM) || defined(CHTEST)
uint8_t dealOrder[MAX_TILES];
#endif
#ifdef CHTEST
const uint8_t *testLayout;
#endif

typedef uint8_t Set[(MAX_TILES + 7) / 8];        // a bit per tile
static Set here;                     // on the table
static Set freeSet;                  // ... and free
static uint8_t nFree, nPairs;
// Where tiles are: bit x2 of [z][y2] is a tile's corner. A row is a word, so
// "is anything beside or on this tile" is a few shifts.
static uint32_t occ[5][16];

struct Move { uint8_t a, b, pay; };  // a == NONE: a shuffle
static Move hist[HIST];
static uint8_t nHist, nShuffles;
static uint32_t seed0;

static inline bool bit(const uint8_t *s, uint8_t i) { return (s[i >> 3] >> (i & 7)) & 1; }
static inline void put(uint8_t *s, uint8_t i) { s[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static inline void drop(uint8_t *s, uint8_t i) { s[i >> 3] &= (uint8_t)~(1u << (i & 7)); }

static uint32_t row(uint8_t z, int y) { return (z < 5 && (unsigned)y < 16) ? occ[z][y] : 0; }

static bool freeAt(uint8_t i) {
    uint8_t x = pos[i].x2, z = pos[i].z;
    int y = pos[i].y2;
    uint32_t over = row((uint8_t)(z + 1), y - 1) | row((uint8_t)(z + 1), y) | row((uint8_t)(z + 1), y + 1);
    if (over & (x ? 7u << (x - 1) : 3u)) return false;
    uint32_t side = row(z, y - 1) | row(z, y) | row(z, y + 1);
    bool l = x >= 2 && ((side >> (x - 2)) & 1), r = (side >> (x + 2)) & 1;
    return !(l && r);
}

// After the table changes: what is left, what is free, what pairs up.
static void refresh() {
    memset(occ, 0, sizeof occ);
    left = 0;
    for (uint8_t i = 0; i < count; i++)
        if (bit(here, i)) { occ[pos[i].z][pos[i].y2] |= 1u << pos[i].x2; left++; }
    memset(freeSet, 0, sizeof freeSet);
    nFree = nPairs = 0;
    uint8_t n[GROUPS];
    memset(n, 0, sizeof n);
    for (uint8_t i = 0; i < count; i++) {
        if (!bit(here, i) || !freeAt(i)) continue;
        put(freeSet, i);
        nFree++;
        nPairs = (uint8_t)(nPairs + n[group(face[i])]++);     // one more pair with each twin already seen
    }
}

void load(uint8_t l) {
    layout = l < LAYOUTS ? l : 0;
    count = 0;
    const uint8_t *runs = LAYOUT[layout];
#ifdef CHTEST
    if (testLayout) runs = testLayout;
#endif
    for (const uint8_t *p = runs; p[0] != 0xFF; p += 4)
        for (uint8_t k = 0; k < p[3] && count < MAX_TILES; k++)
            pos[count++] = Pos{(uint8_t)(p[2] + 2 * k), p[1], p[0]};
    // Drawing order: a layer at a time, and along the diagonals within it, so
    // each tile's shadow (it falls right and down) lands only on tiles drawn
    // before it or is covered by the ones drawn after.
    for (uint8_t i = 1; i < count; i++) {
        Pos t = pos[i];
        uint16_t key = (uint16_t)(t.z << 12 | (t.x2 + t.y2) << 6 | t.x2);
        uint8_t j = i;
        for (; j > 0; j--) {
            const Pos &q = pos[j - 1];
            if ((uint16_t)(q.z << 12 | (q.x2 + q.y2) << 6 | q.x2) <= key) break;
            pos[j] = q;
        }
        pos[j] = t;
    }
    memset(here, 0, sizeof here);
    for (uint8_t i = 0; i < count; i++) put(here, i);
    memset(face, 0, sizeof face);
    nHist = nShuffles = 0;
    chips = 0;
    streak = 0;
    streakT = 0;
    ticks = 0;
    bonus = 0;
    refresh();
}

// ---------------------------------------------------------------------------
// The deal
// ---------------------------------------------------------------------------
static uint32_t rng;
enum : uint8_t { IDLE, DEAL, SHUFFLE };
static uint8_t mode;
static bool failed;
static uint8_t want, placed, tries;
static uint8_t pairList[MAX_TILES / 2][2];
static uint8_t newFace[MAX_TILES];
static Set keep;                     // the tiles being dealt over

static uint32_t rnd(uint32_t n) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng % n;
}

// The faces to deal, two by two and in a random order: a whole set, or
// (some): the faces of the tiles on the table.
static void listPairs(bool some) {
    uint8_t n[FACES];
    for (uint8_t f = 0; f < FACES; f++) n[f] = some ? 0 : (f < FLOWER ? 4 : 1);
    if (some)
        for (uint8_t i = 0; i < count; i++) if (bit(here, i)) n[face[i]]++;
    want = 0;
    for (uint8_t f = 0; f < FLOWER; f++)
        for (uint8_t k = n[f] / 2; k; k--) { pairList[want][0] = pairList[want][1] = f; want++; }
    // Flowers pair with any flower, seasons with any season.
    for (uint8_t g = FLOWER; g < FACES; g += 4) {
        uint8_t odd = NONE;
        for (uint8_t f = g; f < g + 4; f++)
            for (uint8_t k = n[f]; k; k--) {
                if (odd == NONE) { odd = f; continue; }
                pairList[want][0] = odd; pairList[want][1] = f; want++;
                odd = NONE;
            }
    }
    for (uint8_t i = (uint8_t)(want - 1); i > 0; i--) {
        uint8_t j = (uint8_t)rnd(i + 1u);
        uint8_t a = pairList[i][0], b = pairList[i][1];
        pairList[i][0] = pairList[j][0]; pairList[i][1] = pairList[j][1];
        pairList[j][0] = a; pairList[j][1] = b;
    }
}

static void begin(uint8_t m) {
    memcpy(keep, here, sizeof keep);
    mode = m;
    placed = tries = 0;
    failed = false;
}

void dealBegin(uint32_t seed) {
    load(layout);
    seed0 = rng = seed ? seed : 1;
    listPairs(false);
    want = count / 2;                // a smaller layout takes the first pairs of the shuffled set
    begin(DEAL);
}

bool shuffleBegin() {
    if (mode || nShuffles >= MAX_SHUFFLES || left < 2) return false;
    rng = seed0 ^ (0x9E3779B9u * (nShuffles + 1u));
    if (!rng) rng = 1;
    listPairs(true);
    begin(SHUFFLE);
    return true;
}

bool dealing() { return mode != IDLE; }
bool shuffleFailed() { return failed; }

// The k-th free tile.
static uint8_t nth(uint8_t k) {
    for (uint8_t i = 0; i < count; i++)
        if (bit(freeSet, i) && !k--) return i;
    return NONE;
}

static void take(uint8_t a, uint8_t b, uint8_t pay) {
    hist[nHist++] = Move{a, b, pay};
    drop(here, a);
    drop(here, b);
    refresh();
}

bool dealStep(uint8_t budget) {
    if (mode == IDLE) return true;
    while (budget--) {
        refresh();                   // what is free of the tiles not yet given a face
        if (nFree < 2) {
            // A dead end - the last tiles lie on one another: start again. A
            // shuffle that keeps ending here has tiles that cannot be cleared
            // however they are dealt.
            memcpy(here, keep, sizeof here);
            placed = 0;
            if (mode == SHUFFLE && ++tries >= 64) {
                failed = true;
                mode = IDLE;
                refresh();
                return true;
            }
            continue;
        }
        uint8_t ka = (uint8_t)rnd(nFree), kb = (uint8_t)rnd(nFree - 1u);
        if (kb >= ka) kb++;
        uint8_t a = nth(ka), b = nth(kb);
        newFace[a] = pairList[placed][0];
        newFace[b] = pairList[placed][1];
#if defined(CHSIM) || defined(CHTEST)
        dealOrder[2 * placed] = a;
        dealOrder[2 * placed + 1] = b;
#endif
        drop(here, a);
        drop(here, b);
        if (++placed < want) continue;
        // Dealt: every tile has its face, and they all go back on the table.
        memcpy(here, keep, sizeof here);
        for (uint8_t i = 0; i < count; i++) if (bit(here, i)) face[i] = newFace[i];
        if (mode == SHUFFLE) {
            spend(SHUFFLE_COST);
            hist[nHist++] = Move{NONE, nShuffles++, 0};
            streak = 0;
            streakT = 0;
        }
        mode = IDLE;
        refresh();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
bool present(uint8_t i) { return i < count && bit(here, i); }
bool isFree(uint8_t i) { return i < count && bit(freeSet, i); }

uint8_t freeList(uint8_t *list) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < count && n < MAX_FREE; i++) if (bit(freeSet, i)) list[n++] = i;
    return n;
}

uint8_t pairs() { return nPairs; }
bool cleared() { return left == 0; }
bool stuck() { return mode == IDLE && left && !nPairs; }
uint8_t shufflesLeft() { return (uint8_t)(MAX_SHUFFLES - nShuffles); }
uint16_t secs() { uint32_t s = ticks / 60; return s > 0xFFFF ? 0xFFFF : (uint16_t)s; }
void spend(uint16_t cost) { chips = chips > cost ? chips - cost : 0; }

void tick() {
    ticks++;
    if (streakT) streakT--;
}

bool hint(uint8_t &a, uint8_t &b) {
    for (uint8_t i = 0; i < count; i++) {
        if (!bit(freeSet, i)) continue;
        for (uint8_t j = (uint8_t)(i + 1); j < count; j++)
            if (bit(freeSet, j) && group(face[i]) == group(face[j])) { a = i; b = j; return true; }
    }
    return false;
}

bool canMatch(uint8_t a, uint8_t b) {
    return a != b && isFree(a) && isFree(b) && group(face[a]) == group(face[b]);
}

bool match(uint8_t a, uint8_t b) {
    if (mode || !canMatch(a, b) || nHist >= HIST) return false;
    // A pair soon after the last raises the streak; otherwise it starts over.
    streak = !streakT ? 1 : (streak < MAX_STREAK ? (uint8_t)(streak + 1) : MAX_STREAK);
    streakT = STREAK_FRAMES;
    uint8_t pay = (uint8_t)(streak * (group(face[a]) >= FLOWER ? 2 : 1));
    chips += pay * PAIR_PAYS;
    take(a, b, pay);
    if (!left) {
        uint16_t s = secs();
        bonus = (uint16_t)(CLEAR_BONUS + (s < PAR_SECS ? PAR_SECS - s : 0));
        chips += bonus;
    }
    return true;
}

uint8_t lastPay() { return nHist ? (uint8_t)(hist[nHist - 1].pay * PAIR_PAYS) : 0; }

bool canUndo() { return mode == IDLE && left && nHist && hist[nHist - 1].a != NONE; }

bool undo(uint8_t &a, uint8_t &b) {
    if (!canUndo()) return false;
    const Move &m = hist[--nHist];
    a = m.a; b = m.b;
    put(here, a);
    put(here, b);
    spend((uint16_t)(m.pay * PAIR_PAYS));
    streak = 0;
    streakT = 0;
    refresh();
    return true;
}

// ---------------------------------------------------------------------------
// Saving: the deal is its seed, so a game is the seed and what was taken.
// ---------------------------------------------------------------------------
void save(Record &r) {
    memset(&r, 0, sizeof r);
    r.seed = seed0;
    r.chips = chips;
    r.secs = secs();
    r.layout = layout;
    r.n = nHist;
    r.streak = streak;
    r.mark = mark;
    for (uint8_t k = 0; k < nHist; k++) {
        r.ab[k][0] = hist[k].a;
        r.ab[k][1] = hist[k].b;
        r.pay[k >> 1] |= (uint8_t)((hist[k].pay & 15) << ((k & 1) * 4));
    }
}

bool restore(const Record &r) {
    if (r.layout >= LAYOUTS || r.n > HIST) return false;
    layout = r.layout;
    dealBegin(r.seed);
    while (!dealStep(255)) {}
    for (uint8_t k = 0; k < r.n; k++) {
        uint8_t a = r.ab[k][0], b = r.ab[k][1];
        if (a == NONE) {
            if (!shuffleBegin()) return false;
            while (!dealStep(255)) {}
            if (failed) return false;
        } else {
            if (!canMatch(a, b)) return false;
            take(a, b, (uint8_t)((r.pay[k >> 1] >> ((k & 1) * 4)) & 15));
        }
    }
    chips = r.chips;
    ticks = (uint32_t)r.secs * 60;
    streak = r.streak;
    streakT = 0;
    mark = r.mark;
    return left != 0;
}

}  // namespace board
