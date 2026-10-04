// The CPU's cube decisions from the match equity table (see Cube.h).
#pragma GCC optimize("Os", "no-ipa-sra")
#include "Cube.h"

namespace cube {

uint32_t equity(int a, int b, bool post) {
    if (a <= 0) return 65536;
    if (b <= 0) return 0;
    if (a > MAX_AWAY) a = MAX_AWAY;
    if (b > MAX_AWAY) b = MAX_AWAY;
    if (post && (a == 1 || b == 1)) {
        if (a == 1 && b == 1) return 32768;
        return a == 1 ? 65536 - POST[b - 1] : POST[a - 1];
    }
    return MET[a - 1][b - 1];
}

// The least chance (Q16) of winning a game doubled to 2v with which taking
// is worth as much as passing, for the side needing a against b.
static int32_t takePoint(int a, int b, int v, bool post) {
    int32_t pass = (int32_t)equity(a, b - v, post), win = (int32_t)equity(a - 2 * v, b, post);
    int32_t lose = (int32_t)equity(a, b - 2 * v, post);
    // (32-bit: a 64-bit division would bring 1.2 KB of library code.)
    if (win <= lose || pass <= lose) return 0;
    if (pass >= win) return 65536;
    return (int32_t)((uint32_t)(pass - lose) * 65536u / (uint32_t)(win - lose));
}

bool wantsTake(int a, int b, int v, bool post, uint32_t p) {
    int32_t t = takePoint(a, b, v, post);
    // The cube comes back live (it can still be turned to win more): a few
    // percent more of the games are worth taking.
    if (a > 2 * v) t -= 2000;
    return (int32_t)p >= t;
}

bool wantsDouble(int a, int b, int v, bool owned, bool post, uint32_t p, bool gammonish) {
    if (a <= v) return false;                   // a win already takes the match: nothing to gain
    if (post && b == 1 && !owned) return true;  // behind after the Crawford game: double at once
    int32_t cash = 65536 - takePoint(b, a, v, post);   // past this the other side should pass
    if ((int32_t)p >= cash) return !(gammonish && p > 55700 && a > v);   // cash, or play on for the gammon
    // Close to it: the other side will take, but the cube is worth having
    // turned (less so when we own it already: we would give that up).
    int32_t window = owned ? 2600 : 4600;
    return (int32_t)p >= cash - window && p >= 36000;
}

bool gammonish(const bg::Board &b, uint8_t side) {
    uint8_t o = side ^ 1;
    if (b.n[o][bg::OFF]) return false;
    uint8_t back = b.n[o][bg::BAR];
    for (uint8_t q = 19; q <= 24; q++) back += b.n[o][q];      // side's home board, in o's numbering
    return back >= 3;
}

}  // namespace cube
