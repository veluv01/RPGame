// The rules of the board (Words.h): tile values, the premium squares and
// check(), which judges and scores a play.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
#include "Words.h"

namespace wd {

//                              -  A  B  C  D  E  F  G  H  I  J  K  L  M  N  O  P   Q  R  S  T  U  V  W  X  Y   Z  ?
const uint8_t VALUE[28] = {0, 1, 3, 3, 2, 1, 4, 2, 4, 1, 8, 5, 1, 3, 1, 1, 3, 10, 1, 1, 1, 1, 4, 4, 8, 4, 10, 0};
const uint8_t COUNT[28] = {0, 9, 2, 2, 4, 12, 2, 3, 2, 9, 1, 1, 4, 2, 6, 8, 2, 1, 6, 4, 6, 4, 2, 2, 1, 2, 1, 2};

// The top-left quarter (and the middle row and column); the board mirrors
// it both ways. Two squares a byte.
#define P2(a, b) (uint8_t)((a) | (b) << 4)
static const uint8_t QUARTER[8][4] = {
    {P2(TW, 0), P2(0, DL), P2(0, 0), P2(0, TW)},
    {P2(0, DW), P2(0, 0), P2(0, TL), P2(0, 0)},
    {P2(0, 0), P2(DW, 0), P2(0, 0), P2(DL, 0)},
    {P2(DL, 0), P2(0, DW), P2(0, 0), P2(0, DL)},
    {P2(0, 0), P2(0, 0), P2(DW, 0), P2(0, 0)},
    {P2(0, TL), P2(0, 0), P2(0, TL), P2(0, 0)},
    {P2(0, 0), P2(DL, 0), P2(0, 0), P2(DL, 0)},
    {P2(TW, 0), P2(0, DL), P2(0, 0), P2(0, DW)},
};

uint8_t premium(uint8_t cell) {
    uint8_t r = cell / SIZE, c = cell % SIZE;
    if (r > 7) r = (uint8_t)(14 - r);
    if (c > 7) c = (uint8_t)(14 - c);
    return (QUARTER[r][c >> 1] >> ((c & 1) * 4)) & 15;
}

// While check() works, the play's tiles sit on the board marked NEW.
static const uint8_t NEW = 0x40;

static bool canStep(uint8_t cell, uint8_t step, bool forward) {
    if (step == 1) return forward ? cell % SIZE != SIZE - 1 : cell % SIZE != 0;
    return forward ? cell < CELLS - SIZE : cell >= SIZE;
}

static uint8_t wordStart(const uint8_t *b, uint8_t cell, uint8_t step) {
    while (canStep(cell, step, false) && b[cell - step]) cell = (uint8_t)(cell - step);
    return cell;
}

// The word from `start`: its length, its score, how many of its tiles are new.
static uint8_t scoreWord(const uint8_t *b, uint8_t start, uint8_t step, int16_t &score, uint8_t &fresh) {
    uint8_t len = 0, mult = 1, cell = start;
    int16_t sum = 0;
    fresh = 0;
    for (;;) {
        uint8_t v = b[cell];
        int16_t val = (v & BLANK) ? 0 : VALUE[v & LETTER];
        if (v & NEW) {
            fresh++;
            switch (premium(cell)) {
                case DL: val *= 2; break;
                case TL: val *= 3; break;
                case DW: mult *= 2; break;
                case TW: mult *= 3; break;
            }
        }
        sum += val;
        len++;
        if (!canStep(cell, step, true) || !b[cell + step]) break;
        cell = (uint8_t)(cell + step);
    }
    score = (int16_t)(sum * mult);
    return len;
}

uint8_t check(uint8_t *b, const Placement *p, uint8_t n, Result &r) {
    r.nWords = 0;
    r.score = 0;
    if (!n || n > RACK) return E_LINE;
    bool row = true, col = true;
    for (uint8_t i = 0; i < n; i++) {
        if (b[p[i].cell]) return E_TAKEN;
        for (uint8_t k = 0; k < i; k++) if (p[k].cell == p[i].cell) return E_TAKEN;
        row &= p[i].cell / SIZE == p[0].cell / SIZE;
        col &= p[i].cell % SIZE == p[0].cell % SIZE;
    }
    if (!row && !col) return E_LINE;
    bool opening = !b[CENTRE];              // nothing has been played yet
    for (uint8_t i = 0; i < n; i++) b[p[i].cell] = (uint8_t)(p[i].tile | NEW);

    uint8_t err = OK, step = row ? 1 : SIZE;
    if (n == 1) {
        // One tile: its word runs whichever way it has a neighbour.
        uint8_t c = p[0].cell;
        bool across = (canStep(c, 1, false) && b[c - 1]) || (canStep(c, 1, true) && b[c + 1]);
        step = across ? 1 : SIZE;
    }
    uint8_t fresh;
    int16_t s;
    uint8_t start = wordStart(b, p[0].cell, step);
    uint8_t len = scoreWord(b, start, step, s, fresh);
    bool touching = len > n;
    if (fresh != n) err = E_GAP;
    else if (len >= 2) {
        r.word[r.nWords++] = {start, len, step};
        r.score = s;
    }
    if (!err) {
        uint8_t other = step == 1 ? SIZE : 1;
        for (uint8_t i = 0; i < n; i++) {
            uint8_t cs = wordStart(b, p[i].cell, other);
            uint8_t cl = scoreWord(b, cs, other, s, fresh);
            if (cl < 2) continue;
            touching = true;
            r.word[r.nWords++] = {cs, cl, other};
            r.score = (int16_t)(r.score + s);
        }
        if (opening) {
            if (!(b[CENTRE] & NEW)) err = E_CENTRE;
            else if (len < 2) err = E_ALONE;
        } else if (!touching || !r.nWords) err = E_ALONE;
    }
    if (n == RACK) r.score = (int16_t)(r.score + BINGO);
    for (uint8_t i = 0; i < n; i++) b[p[i].cell] = 0;
    return err;
}

void letters(const uint8_t *b, const Placement *p, uint8_t n, const Span &s, uint8_t *out) {
    for (uint8_t i = 0; i < s.len; i++) {
        uint8_t cell = (uint8_t)(s.start + i * s.step), v = b[cell];
        for (uint8_t k = 0; k < n; k++) if (p[k].cell == cell) v = p[k].tile;
        out[i] = v & LETTER;
    }
}

}  // namespace wd
