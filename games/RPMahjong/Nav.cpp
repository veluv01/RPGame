#pragma GCC optimize("Os")
// Where the D-pad takes the cursor (Nav.h).
#include "Nav.h"
#include "MahjongBoard.h"

namespace nav {

static inline int cx(uint8_t i) { return board::px(i) + 4; }
static inline int cy(uint8_t i) { return board::py(i) + 6; }
// Reading order: rows a tile high, left to right within one. No two tiles
// share a key (the index settles a tie), so stepping along it visits them all.
static inline int32_t key(uint8_t i) { return ((int32_t)(cy(i) / 12) << 16) | (cx(i) << 8) | i; }

uint8_t step(const uint8_t *list, uint8_t n, uint8_t from, int ux, int uy) {
    uint8_t best = board::NONE, back = board::NONE;
    int32_t bestS = 0x7FFFFFFF, backS = 0x7FFFFFFF;
    int fx = cx(from), fy = cy(from);
    int32_t fk = key(from);
    for (uint8_t i = 0; i < n; i++) {
        uint8_t t = list[i];
        if (t == from) continue;
        int32_t sc;
        bool ahead;
        if (ux) {
            // The next in reading order that way; past the end, round to the other end.
            int32_t d = (key(t) - fk) * ux;
            ahead = d > 0;
            sc = d;
        } else {
            int dx = cx(t) - fx, dy = cy(t) - fy;
            int along = dy * uy, side = dx < 0 ? -dx : dx;
            ahead = along > 0;
            sc = ahead ? along + 2 * side : 4 * along + side;
        }
        if (ahead && sc < bestS) { bestS = sc; best = t; }
        if (!ahead && sc < backS) { backS = sc; back = t; }
    }
    return best != board::NONE ? best : back;
}

uint8_t nearest(const uint8_t *list, uint8_t n, uint8_t from) {
    uint8_t best = board::NONE;
    int bestD = 0x7FFF;
    for (uint8_t i = 0; i < n; i++) {
        int dx = cx(list[i]) - cx(from), dy = cy(list[i]) - cy(from);
        int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (d < bestD) { bestD = d; best = list[i]; }
    }
    return best;
}

}  // namespace nav
