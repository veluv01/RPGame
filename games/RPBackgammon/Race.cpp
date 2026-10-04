// The race count from its table, src/ai/RaceData.cpp (see Race.h).
#pragma GCC optimize("Os", "no-ipa-sra")
#include "Race.h"

namespace race {

int16_t cost(const bg::Board &b, uint8_t s) {
    const uint8_t *n = b.n[s];
    if (n[bg::OFF] == bg::CHECKERS) return 0;
    int c = TABLE[24];
    const uint8_t *t = TABLE;
    for (uint8_t p = 1; p <= 6; p++, t += 4) {
        uint8_t k = n[p];
        if (k >= 1) c += t[0];
        if (k >= 2) c += t[1];
        if (k >= 3) c += t[2];
        if (k > 3) c += t[3] * (k - 3);
    }
    // Still on the way home: as a checker more on the 6 point, and two a pip
    // (a roll is worth a little over 8 pips).
    for (uint8_t p = 7; p <= bg::BAR; p++) c += n[p] * (TABLE[23] + 2 * (p - 6));
    return (int16_t)c;
}

}  // namespace race
