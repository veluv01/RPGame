// The two layouts' spots (Zones.h) and the cursor's moves between them.
#pragma GCC optimize("Os")   // cold code: size over speed
#include "Zones.h"
#include "Craps.h"

namespace zones {

// Bar: four 17 px chip slots and a 50 px ROLL button, 2 px apart (Bar.cpp).
#define BAR_ZONES \
    {1, 113, 17, 14, Z_CHIP0, 9, 119}, {20, 113, 17, 14, Z_CHIP0 + 1, 28, 119}, \
    {39, 113, 17, 14, Z_CHIP0 + 2, 47, 119}, {58, 113, 17, 14, Z_CHIP0 + 3, 66, 119}, \
    {77, 113, 50, 14, Z_ROLL, 101, 119}

// Classic: the line bets on the left, the centre props on the right.
//   46..67  six number boxes (place bets; come points and their odds)
//   68..76  COME          77..89  FIELD
//   90..98  DON'T PASS BAR | LAY      99..110  PASS LINE | ODDS (behind the line)
//   x 90..127: ANY 7, the hardways (6 10 / 8 4), YO, ANY CRAPS
static const Zone CLASSIC[] = {
    {0, 46, 15, 22, PLACE4, 7, 62},    {15, 46, 15, 22, PLACE5, 22, 62},  {30, 46, 15, 22, PLACE6, 37, 62},
    {45, 46, 15, 22, PLACE8, 52, 62},  {60, 46, 15, 22, PLACE9, 67, 62},  {75, 46, 15, 22, PLACE10, 82, 62},
    {1, 53, 13, 7, CODDS4, 9, 55},     {16, 53, 13, 7, CODDS5, 24, 55},   {31, 53, 13, 7, CODDS6, 39, 55},
    {46, 53, 13, 7, CODDS8, 54, 55},   {61, 53, 13, 7, CODDS9, 69, 55},   {76, 53, 13, 7, CODDS10, 84, 55},
    {0, 68, 90, 9, COME, 72, 71},
    {0, 77, 90, 13, FIELD, 31, 86},
    {0, 90, 68, 9, DONT, 62, 93},      {68, 90, 22, 9, DONT_ODDS, 83, 94},
    {0, 99, 68, 12, PASS, 62, 104},    {68, 99, 22, 12, PASS_ODDS, 81, 106},
    {90, 46, 38, 10, ANY7, 121, 50},
    {90, 56, 19, 14, HARD6, 99, 64},   {109, 56, 19, 14, HARD10, 118, 64},
    {90, 70, 19, 14, HARD8, 99, 78},   {109, 70, 19, 14, HARD4, 118, 78},
    {90, 84, 38, 12, YO, 120, 89},
    {90, 96, 38, 15, ANYCRAPS, 109, 105},
    BAR_ZONES
};

// Beginner: the same table with the extras gone and the rest roomier. All
// six numbers stay (the puck marks the point on them); only 6 and 8 take
// place bets.
static const Zone BEGINNER[] = {
    {1, 46, 21, 26, PLACE4, 11, 64},   {22, 46, 21, 26, PLACE5, 32, 64},  {43, 46, 21, 26, PLACE6, 53, 64},
    {64, 46, 21, 26, PLACE8, 74, 64},  {85, 46, 21, 26, PLACE9, 95, 64},  {106, 46, 21, 26, PLACE10, 116, 64},
    {0, 72, 128, 15, FIELD, 50, 82},
    {0, 87, 84, 11, DONT, 76, 91},     {84, 87, 44, 11, DONT_ODDS, 110, 91},
    {0, 98, 84, 13, PASS, 76, 104},    {84, 98, 44, 13, PASS_ODDS, 110, 104},
    BAR_ZONES
};

static const Zone *table(uint8_t t) { return t == TABLE_BEGINNER ? BEGINNER : CLASSIC; }

uint8_t count(uint8_t t) {
    return t == TABLE_BEGINNER ? (uint8_t)(sizeof BEGINNER / sizeof BEGINNER[0])
                               : (uint8_t)(sizeof CLASSIC / sizeof CLASSIC[0]);
}

const Zone &at(uint8_t t, uint8_t i) { return table(t)[i]; }

uint8_t find(uint8_t t, uint8_t bet) {
    for (uint8_t i = 0; i < count(t); i++) if (table(t)[i].bet == bet) return i;
    return 0xFF;
}

bool inBar(uint8_t t, uint8_t i) { return at(t, i).bet >= Z_CHIP0; }

bool usable(const Craps &g, uint8_t i) {
    const Zone &z = at(g.opt.table, i);
    if (z.bet >= Z_CHIP0) return true;
    if (!g.onTable(z.bet)) return false;
    if (z.bet >= CODDS4) return g.bet[COME4 + (z.bet - CODDS4)] != 0;
    return true;
}

bool anchor(uint8_t t, uint8_t bet, int &x, int &y) {
    if (bet >= COME4 && bet < CODDS4) {                  // come point: left of its odds
        uint8_t i = find(t, (uint8_t)(CODDS4 + (bet - COME4)));
        if (i == 0xFF) return false;
        x = at(t, i).ax - 4; y = at(t, i).ay + 2;
        return true;
    }
    uint8_t i = find(t, bet);
    if (i == 0xFF) return false;
    x = at(t, i).ax; y = at(t, i).ay;
    return true;
}

uint8_t nearest(const Craps &g, uint8_t from, int ux, int uy) {
    uint8_t t = g.opt.table, n = count(t);
    if (from >= n) from = 0;
    int fx = at(t, from).ax, fy = at(t, from).ay;
    uint8_t best = 0xFF, back = 0xFF;
    int32_t bestS = 0x7FFFFFFF, backS = 0x7FFFFFFF;
    for (uint8_t i = 0; i < n; i++) {
        if (i == from || !usable(g, i)) continue;
        int dx = at(t, i).ax - fx, dy = at(t, i).ay - fy;
        int along = dx * ux + dy * uy, side = dx * uy - dy * ux;
        if (side < 0) side = -side;
        if (!ux && !uy) along = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        int32_t sc = along > 0 ? along + 2 * side : 4 * along + side;
        if (along > 0 && sc < bestS) { bestS = sc; best = i; }
        if (along <= 0 && sc < backS) { backS = sc; back = i; }
    }
    return best != 0xFF ? best : back;
}

}  // namespace zones
