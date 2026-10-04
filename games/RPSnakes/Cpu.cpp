// The CPU's choice of die in ARCADE (Cpu.h), and the SHARK's TURNS[].
#pragma GCC optimize("Os")   // cold code: size over speed
#include "Cpu.h"
#include "Game.h"

namespace cpu {

using namespace game;
using namespace layout;

// From each square, the turns the best play still needs to get home, on
// average, in eighths (tools/turns.py works it out; the host tests check it
// against the board).
const uint8_t TURNS[101] = {
    0, 131, 128, 127, 125, 124, 124, 123, 124, 117, 117, 117, 117, 117, 118, 115, 116, 118, 120, 121,
    122, 133, 132, 130, 129, 128, 126, 124, 124, 122, 120, 119, 117, 115, 113, 113, 111, 109, 107, 105,
    103, 102, 101, 100, 95, 95, 95, 94, 92, 92, 96, 94, 93, 92, 94, 93, 92, 86, 87, 87,
    87, 85, 84, 91, 88, 86, 86, 78, 79, 78, 78, 78, 78, 84, 82, 81, 80, 78, 76, 74,
    73, 73, 71, 68, 67, 65, 64, 63, 59, 57, 58, 55, 54, 57, 44, 49, 43, 43, 49, 44,
    0,
};

// Where a token bumped off `sq` comes to rest.
static uint8_t dropped(uint8_t sq) { return landing(below(sq), 0); }

// What resting on `sq` costs p: the turns left from there, in 36ths of an
// eighth; less what the rivals it bumps lose, plus what it stands to lose
// if one of them bumps it there.
static int32_t cost(uint8_t p, uint8_t sq) {
    int32_t s = TURNS[sq] * 36;
    if (sq == 1) return s;
    for (uint8_t r = 0; r < st.players; r++) {
        if (r == p) continue;
        uint8_t at = st.pos[r];
        if (at == sq) { s -= (TURNS[dropped(at)] - TURNS[at]) * 18; continue; }
        // With k of the six faces landing it here, one of its two dice
        // shows one 36 - (6 - k)^2 times in 36.
        uint8_t k = 0;
        for (uint8_t f = 1; f <= 6; f++) k += landing(at, f) == sq;
        s += (36 - (6 - k) * (6 - k)) * (TURNS[dropped(sq)] - TURNS[sq]);
    }
    return s;
}

uint8_t pick(uint8_t p, uint8_t d1, uint8_t d2) {
    uint8_t a = landing(st.pos[p], d1), b = landing(st.pos[p], d2);
    if (a == LAST) return 0;
    if (b == LAST) return 1;
    switch (st.kind[p] - CPU) {
        case 0: return (uint8_t)(((st.rng >> 9) ^ st.turn) & 1);
        case 1: return b > a;
        default: return cost(p, b) < cost(p, a);
    }
}

}  // namespace cpu
