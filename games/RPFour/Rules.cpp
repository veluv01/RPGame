// The rules on bitboards (see Rules.h): a four, the cells that would make
// one, and the four through a cell.
#pragma GCC optimize("O2")       // the CPU's search lives in these few functions
#include "Rules.h"
#include <rpgame/RamFunc.h>

namespace c4 {

void reset(Board &b) {
    b.side[0] = b.side[1] = 0;
    for (uint8_t c = 0; c < COLS; c++) b.h[c] = 0;
    b.n = 0;
}

bool hasFour(Bits p) {
    static const uint8_t DIR[4] = {1, 7, 6, 8};         // up, across, the two diagonals
    for (uint8_t d : DIR) {
        Bits m = p & (p >> d);
        if (m & (m >> (2 * d))) return true;
    }
    return false;
}

// After Pascal Pons's solver: for each direction, the cells next to three
// in a line, or in the gap of two and one.
RAMFUNC(winning) Bits winning(Bits p, Bits occupied) {
    Bits r = (p << 1) & (p << 2) & (p << 3);            // on top of three
    Bits q;
#define LINE(s) \
    q = (p << (s)) & (p << 2 * (s)); \
    r |= q & (p << 3 * (s)); \
    r |= q & (p >> (s)); \
    q = (p >> (s)) & (p >> 2 * (s)); \
    r |= q & (p << (s)); \
    r |= q & (p >> 3 * (s));
    LINE(7)
    LINE(6)
    LINE(8)
#undef LINE
    return r & (BOARD ^ occupied);
}

RAMFUNC(count) uint8_t count(Bits x) {
    uint32_t lo = (uint32_t)x, hi = (uint32_t)(x >> 32);
    uint8_t n = 0;
    while (lo) { lo &= lo - 1; n++; }
    while (hi) { hi &= hi - 1; n++; }
    return n;
}

uint8_t columnOf(Bits x) {
    for (uint8_t c = 0; c < COLS; c++, x >>= 7)
        if (x & 0x7F) return c;
    return 0xFF;
}

bool fourThrough(Bits p, uint8_t col, uint8_t row, uint8_t cells[4]) {
    static const int8_t DC[4] = {0, 1, 1, 1}, DR[4] = {1, 0, 1, -1};
    for (uint8_t d = 0; d < 4; d++) {
        // Walk back to the start of the run through the cell, then count it.
        int c = col, r = row, idx = 0;
        while (c - DC[d] >= 0 && r - DR[d] >= 0 && r - DR[d] < ROWS && (p & cell((uint8_t)(c - DC[d]), (uint8_t)(r - DR[d])))) {
            c -= DC[d]; r -= DR[d]; idx++;
        }
        int n = 0, cc = c, rr = r;
        while (cc < COLS && rr >= 0 && rr < ROWS && (p & cell((uint8_t)cc, (uint8_t)rr))) { n++; cc += DC[d]; rr += DR[d]; }
        if (n < 4) continue;
        // Four of the run that include the cell.
        int k = idx > 3 ? idx - 3 : 0;
        c += k * DC[d]; r += k * DR[d];
        for (uint8_t i = 0; i < 4; i++) cells[i] = (uint8_t)((c + i * DC[d]) * 7 + r + i * DR[d]);
        return true;
    }
    return false;
}

}  // namespace c4
