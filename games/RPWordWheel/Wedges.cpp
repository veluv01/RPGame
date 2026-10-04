#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// The wheel as printed (Wedges.h): the base layout, and what each round changes.
#include "Wedges.h"

namespace wedge {

// A money wedge is its value / 50; anything from S on is special.
enum : uint8_t { S = 0x80, S_TOP = S, S_BANK, S_LOSE, S_FREE, S_WILD, S_PRIZE, S_MYST, S_THIRDS };

static const uint8_t BASE[COUNT] = {
    S_TOP, 12, 14, 12, 13, 10, 14, S_BANK, 12, S_PRIZE, 10, 12,
    S_BANK, 13, S_FREE, 14, S_LOSE, 16, 10, 13, 10, 18, S_WILD, 11,
};

// Per round: the top-dollar value, then (index, wedge) changes.
static const uint16_t TOP_VALUE[3] = {2500, 3500, 5000};
static const uint8_t PATCH[3][3][2] = {
    {{0, S_TOP}, {0, S_TOP}, {0, S_TOP}},
    {{9, S_MYST}, {21, S_MYST}, {22, 12}},
    {{9, 16}, {22, 14}, {18, S_THIRDS}},
};

Wedge at(uint8_t round, uint8_t index) {
    if (round > 2) round = 2;
    uint8_t b = BASE[index % COUNT];
    for (uint8_t i = 0; i < 3; i++)
        if (PATCH[round][i][0] == index) b = PATCH[round][i][1];
    static const uint8_t KIND[8] = {TOP, BANKRUPT, LOSE, FREE, WILD, PRIZE, MYSTERY, THIRDS};
    static const uint16_t VALUE[8] = {0, 0, 0, FREE_VALUE, TOKEN_VALUE, TOKEN_VALUE, MYSTERY_VALUE, BIG};
    Wedge w;
    if (b < S) {
        w.kind = MONEY;
        w.value = (uint16_t)(b * 50);
    } else {
        w.kind = KIND[b - S];
        w.value = b == S_TOP ? TOP_VALUE[round] : VALUE[b - S];
    }
    return w;
}

}  // namespace wedge
