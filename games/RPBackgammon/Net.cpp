// The network's arithmetic (see Net.h): a position's rows into the hidden
// sums, kept from one play to the next, and the sigmoid as a table.
#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Net.h"
#include <rpgame/RamFunc.h>

namespace net {

// The logistic curve 65535 / (1 + e^-x) at x = -8, -7.75 .. 8.
static const uint16_t SIGMOID[65] = {
    22, 28, 36, 47, 60, 77, 98, 126, 162, 208, 267, 342, 439, 562, 720, 922, 1179, 1506, 1921, 2446, 3108,
    3938, 4971, 6249, 7812, 9702, 11955, 14595, 17625, 21025, 24742, 28693, 32768, 36842, 40793, 44510, 47910,
    50940, 53580, 55833, 57723, 59286, 60564, 61597, 62427, 63089, 63614, 64029, 64356, 64613, 64815, 64973,
    65096, 65193, 65268, 65327, 65373, 65409, 65437, 65458, 65475, 65488, 65499, 65507, 65513,
};

// 65535 * logistic(x), x in 1/256ths.
static inline __attribute__((always_inline)) uint16_t sigmoid(int32_t x) {
    x += 8 * 256;
    if (x < 0) x = 0;
    if (x > 16 * 256 - 1) x = 16 * 256 - 1;
    uint32_t i = (uint32_t)x >> 6, f = (uint32_t)x & 63;
    return (uint16_t)(SIGMOID[i] + (((int32_t)SIGMOID[i + 1] - SIGMOID[i]) * (int32_t)f >> 6));
}

// The last position evaluated, and its hidden sums. The CPU evaluates the
// plays of a roll one after another, and each differs from the one before
// in a point or two: only the rows of the points that changed are added or
// taken away (a few rows, not the thirty a position has). The sums are
// modular 16-bit integers, so the order they are added in cannot change them.
static bg::Board seen;
static uint8_t seenMover = 0xFF;
static int16_t acc[H];

void forget() { seenMover = 0xFF; }

// The hot loop: one row of the first table into the running sums.
RAMFUNC(netrow) static void addRow(const int8_t *w, int times) {
    for (int j = 0; j < H; j++) acc[j] = (int16_t)(acc[j] + w[j] * times);
}

// A point's checkers going from a to b: one row for each checker level
// gained or lost (the first, second, third, then the "one more" row).
static inline __attribute__((always_inline)) void point(int base, int a, int b) {
    while (a < b) { a++; addRow(HID_W[base + (a > 3 ? 3 : a - 1)], 1); }
    while (a > b) { addRow(HID_W[base + (a > 3 ? 3 : a - 1)], -1); a--; }
}

RAMFUNC(neteval) int32_t eval(const bg::Board &b, uint8_t mover) {
    if (mover != seenMover) {
        // From nothing: no checkers anywhere, then every difference is added.
        for (int j = 0; j < H; j++) acc[j] = HID_B[j];
        memset(seen.n, 0, sizeof seen.n);
        seenMover = mover;
    }
    // Word by word, then the points of the words that differ.
    const uint32_t *now = (const uint32_t *)b.n, *was = (const uint32_t *)seen.n;
    for (int w = 0; w < (int)(sizeof b.n / 4); w++) {
        if (now[w] == was[w]) continue;
        for (int k = 4 * w; k < 4 * w + 4; k++) {
            int a = (&seen.n[0][0])[k], c = (&b.n[0][0])[k];
            if (a == c) continue;
            int side = k >= 26, p = k - 26 * side;
            int base = (side == mover ? 0 : SIDE_ROWS);
            if (p == bg::BAR) addRow(HID_W[base + 96], c - a);
            else if (p == bg::OFF) addRow(HID_W[base + 97], c - a);
            else point(base + (p - 1) * 4, a, c);
        }
    }
    memcpy(seen.n, b.n, sizeof seen.n);
    int32_t out = OUT_B;
    for (int j = 0; j < H; j++) out += OUT_W[j] * (int32_t)(sigmoid(acc[j] * (256 / W1_SCALE)) >> 8);
    return out;
}

uint16_t chance(int32_t e) { return sigmoid(e / W2_SCALE); }

}  // namespace net
