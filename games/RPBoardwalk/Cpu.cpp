// The CPU players' judgement (Cpu.h). A level is a row of LEVEL[]: the
// cash it keeps back and how high it bids.
#pragma GCC optimize("Os")   // cold code: size over speed
#include "Cpu.h"
#include "Game.h"

namespace cpu {

using namespace game;
using namespace board;

// Cash kept back when buying, bids as a percentage of what a deed is worth
// to it, cash kept back when building.
struct Level { int16_t reserve; uint8_t bidPct; int16_t buildReserve; };
static const Level LEVEL[LEVELS] = {
    {400, 70, 500},     // careful: buys little, bids low, builds late
    {200, 100, 300},
    {50, 120, 150},     // a shark
};
static const Level &lv(uint8_t p) { return LEVEL[st.pl[p].kind - CPU]; }

// What t is worth to p, as a percentage of its list price: more as it
// brings a colour group within reach (most of a group builds houses, all of
// it doubles the bare rent), or keeps one from someone else.
static int worth(uint8_t p, uint8_t t) {
    uint8_t tiles[4], other[SEATS] = {0, 0, 0, 0}, mine = 0, theirs = 0;
    uint8_t n = groupTiles(group(t), tiles);
    for (uint8_t i = 0; i < n; i++) {
        uint8_t o = owner(tiles[i]);
        if (tiles[i] == t || o == BANK) continue;
        if (o == p) mine++;
        else if (++other[o] > theirs) theirs = other[o];
    }
    if (type(t) != STREET) return 100 + 15 * mine;      // railroads and utilities: each is worth more than the last
    if (mine + 1 == n) return 160;
    if (2 * (mine + 1) > n) return 150;
    if (2 * (theirs + 1) > n) return 130;
    return mine ? 120 : 100;
}

int valuation(uint8_t p, uint8_t t) {
    int pct = worth(p, t);
    int v = price(t) * pct / 100 * lv(p).bidPct / 100;
    int cash = (int)st.pl[p].cash - (pct >= 150 ? 0 : lv(p).reserve / 2);
    if (v > cash) v = cash;
    return v < 0 ? 0 : v;
}

bool wantsBuy(uint8_t p, uint8_t t) {
    return worth(p, t) >= 130 || st.pl[p].cash - price(t) >= lv(p).reserve;
}

// The house that adds the most rent for its cost.
int pickBuild(uint8_t p) {
    int best = -1, bestScore = 0;
    for (uint8_t t = 0; t < TILES; t++) {
        if (!canBuild(t) || st.pl[p].cash - houseCost(t) < lv(p).buildReserve) continue;
        int score = (RENT[TILE[t].aux][level(t) + 1] - rent(t, 0)) * 100 / houseCost(t);
        if (score > bestScore) { best = t; bestScore = score; }
    }
    return best;
}

// The card if it has one; early on, with deeds still to be had, it pays to
// get out and about; later, jail is the safest square on the board.
uint8_t jailChoice(uint8_t p) {
    if (st.pl[p].flags & (F_CARD | F_CARD << 1)) return 2;
    uint8_t unowned = 0;
    for (uint8_t t = 0; t < TILES; t++) unowned += isDeed(t) && owner(t) == BANK;
    return unowned > 6 && st.pl[p].cash >= 50 + lv(p).reserve ? 1 : 0;
}

}  // namespace cpu
