// The rules of every table (Rules.h): MODES[] gives each its grid, line
// length and flags; one line scanner serves them all.
#pragma GCC optimize("Os")
#include "Rules.h"
#include <string.h>

const ModeDef MODES[MODE_COUNT] = {
    {3, 3, 3, 0, 1, 1},              // CLASSIC
    {3, 3, 3, F_BLITZ, 1, 1},        // BLITZ (pays by boards won)
    {3, 3, 3, F_MISERE, 1, 1},       // MISERE
    {3, 3, 3, F_MISERE | F_SAME, 1, 1},   // ALL X (notakto)
    {3, 3, 3, F_VANISH, 1, 1},       // VANISH
    {3, 3, 3, F_GOBBLE, 3, 2},       // GOBBLE
    {3, 3, 3, F_WILD, 1, 1},         // WILD
    {3, 3, 3, F_DARK, 2, 1},         // DARK (phantom)
    {3, 3, 3, F_COIN, 1, 1},         // COIN FLIP
    {3, 3, 3, F_AUCTION, 2, 1},      // AUCTION
    {5, 5, 4, 0, 2, 1},              // BIG 5
    {5, 5, 4, F_WRAP, 1, 1},         // WRAP
    {5, 5, 4, F_MINES, 3, 1},        // MINES
    {7, 6, 4, F_GRAVITY, 2, 1},      // DROP 4
    {9, 9, 3, F_ULTIMATE, 5, 1},     // ULTIMATE
    {11, 9, 5, 0, 5, 1},             // THE 99
};

const Dir DIRS[4] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

const uint8_t LINES3[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6},
};

namespace rules {

void start(Board &b, Mode m) {
    memset(&b, 0, sizeof b);
    const ModeDef &d = MODES[m];
    b.w = d.w; b.h = d.h; b.k = d.k; b.flags = d.flags;
    b.n = b.left = (uint8_t)(d.w * d.h);
    b.last = b.gone = b.must = b.smallWon = NONE;
    memset(b.win, NONE, sizeof b.win);
    memset(b.stock, 2, sizeof b.stock);
    b.chips[0] = b.chips[1] = AUCTION_CHIPS;
}

bool legal(const Board &b, uint8_t cell, uint8_t arg) {
    if (b.result || cell >= b.n) return false;
    uint8_t c = b.cell[cell];
    if (b.flags & F_GOBBLE) return arg < 3 && b.stock[b.turn][arg] && (!c || levelOf(c) < arg);
    if (c || arg > ((b.flags & F_WILD) ? 1 : 0)) return false;
    if (b.flags & F_GRAVITY) return cell + b.w >= b.n || b.cell[cell + b.w];
    if (b.flags & F_ULTIMATE) {
        uint8_t s = smallOf(cell);
        return !b.small[s] && (b.must == NONE || b.must == s);
    }
    return true;
}

bool anyLegal(const Board &b) {
    for (uint8_t c = 0; c < b.n; c++)
        for (uint8_t a = 0; a < 3; a++) if (legal(b, c, a)) return true;
    return false;
}

uint8_t drop(const Board &b, uint8_t cell) {
    uint8_t c = (uint8_t)(cell % b.w + b.n - b.w);       // the column's bottom cell
    while (b.cell[c]) {
        if (c < b.w) return NONE;
        c = (uint8_t)(c - b.w);
    }
    return c;
}

// The longest line of who through cell, inside the box x0..x1, y0..y1
// (exclusive ends; WRAP: no ends); win gets up to five of its cells, in order.
static uint8_t run(const Board &b, uint8_t cell, uint8_t who, int x0, int y0, int x1, int y1, uint8_t *win) {
    int x = cell % b.w, y = cell / b.w;
    bool wrap = (b.flags & F_WRAP) != 0;
    uint8_t best = 0;
    for (uint8_t i = 0; i < 4; i++) {
        const Dir &d = DIRS[i];
        uint8_t n = 1, first = cell;
        for (int sg = 1; sg >= -1; sg -= 2) {
            int cx = x, cy = y;
            while (n < 5) {
                cx += sg * d.dx; cy += sg * d.dy;
                if (wrap) { cx = (cx + b.w) % b.w; cy = (cy + b.h) % b.h; }
                else if (cx < x0 || cx >= x1 || cy < y0 || cy >= y1) break;
                uint8_t c = (uint8_t)(cy * b.w + cx);
                if (topOf(b.cell[c]) != who) break;
                n++;
                if (sg < 0) first = c;
            }
        }
        if (n <= best) continue;
        best = n;
        // The cells, from the far end back through this one and on.
        int cx = first % b.w, cy = first / b.w;
        for (uint8_t j = 0; j < 5; j++) {
            win[j] = j < n ? (uint8_t)(cy * b.w + cx) : NONE;
            cx += d.dx; cy += d.dy;
            if (wrap) { cx = (cx + b.w) % b.w; cy = (cy + b.h) % b.h; }
        }
    }
    return best;
}

void play(Board &b, uint8_t cell, uint8_t arg) {
    uint8_t side = b.turn, who = (uint8_t)(side + 1), lvl = 0;
    b.boom = 0;
    if ((b.flags & F_MINES) && (b.mines >> cell & 1)) {  // the mark is gone, and the cell is nobody's
        b.cell[cell] = 3;
        b.left--;
        b.last = cell;
        b.run = 0;
        b.boom = 1;
        b.turn = side ^ 1;
        if (!b.left) b.result = R_DRAW;
        return;
    }
    if (b.flags & F_WILD) who = (uint8_t)(arg + 1);
    if (b.flags & F_SAME) who = 1;
    if (b.flags & F_GOBBLE) { lvl = arg; b.stock[side][lvl]--; }
    if (!b.cell[cell]) b.left--;
    b.cell[cell] = (uint8_t)(b.cell[cell] | (who << (2 * lvl)));
    b.last = cell;
    b.gone = b.smallWon = NONE;
    if (b.flags & F_VANISH) {
        uint8_t *q = b.q[side];
        if (b.qn[side] == 3) {
            b.gone = q[0];
            b.cell[q[0]] = 0;
            b.left++;
            q[0] = q[1]; q[1] = q[2];
            b.qn[side] = 2;
        }
        q[b.qn[side]++] = cell;
    }

    int x0 = 0, y0 = 0, x1 = b.w, y1 = b.h;
    if (b.flags & F_ULTIMATE) {
        x0 = cell % 9 / 3 * 3; y0 = cell / 27 * 3;
        x1 = x0 + 3; y1 = y0 + 3;
    }
    uint8_t win[5];
    b.run = run(b, cell, who, x0, y0, x1, y1, win);
    bool line = b.run >= b.k;

    if (b.flags & F_ULTIMATE) {
        uint8_t s = smallOf(cell);
        if (line) b.small[s] = who;
        else {
            bool full = true;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) if (!b.cell[yy * 9 + xx]) full = false;
            if (full) b.small[s] = 3;
        }
        if (b.small[s]) b.smallWon = s;
        uint8_t dst = (uint8_t)(cell / 9 % 3 * 3 + cell % 3);
        b.must = b.small[dst] ? NONE : dst;
        if (line) {
            for (uint8_t i = 0; i < 8 && !b.result; i++) {
                const uint8_t *l = LINES3[i];
                if (b.small[l[0]] == who && b.small[l[1]] == who && b.small[l[2]] == who) {
                    b.result = who;
                    for (uint8_t j = 0; j < 3; j++) b.win[j] = smallCentre(l[j]);
                }
            }
        }
        if (!b.result) {                // every board decided: on points
            int8_t open = 0, lead = 0;
            for (uint8_t i = 0; i < 9; i++) {
                if (!b.small[i]) open++;
                else if (b.small[i] == 1) lead++;
                else if (b.small[i] == 2) lead--;
            }
            if (!open) b.result = lead > 0 ? R_P0 : (lead < 0 ? R_P1 : R_DRAW);
        }
    } else if (line) {
        memcpy(b.win, win, sizeof win);
        b.result = (uint8_t)(((b.flags & F_MISERE) ? side ^ 1 : side) + 1);
    }
    b.turn = side ^ 1;
    if (!b.result) {
        if (b.flags & F_GOBBLE) { if (!anyLegal(b)) b.result = R_DRAW; }
        else if (!b.left) b.result = R_DRAW;
    }
}

}  // namespace rules
