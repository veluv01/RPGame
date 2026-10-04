// The hand evaluator (Hand.h): rank masks and counts, no lookup tables, and
// the names of hands ("PAIR OF KINGS").
#pragma GCC optimize("Os")
#include "Hand.h"
#include "Cards.h"

namespace hand {

// The highest set bit (rank 0..12) of a non-empty mask.
static inline uint8_t hi(uint32_t m) {
    uint8_t r = 12;
    while (!((m >> r) & 1)) r--;
    return r;
}

// Appends the top k ranks of m to the score as nibbles (zeros once m runs out).
static uint32_t kick(uint32_t s, uint32_t m, uint8_t k) {
    for (; k; k--) {
        uint8_t r = 0;
        if (m) { r = hi(m); m &= ~(1u << r); }
        s = (s << 4) | r;
    }
    return s;
}

// The top rank of the best straight in a rank mask, or -1. The ace also
// plays low (A-2-3-4-5, the wheel, is five high).
static int straightHigh(uint32_t m) {
    uint32_t w = (m << 1) | ((m >> 12) & 1);              // bit 0: the ace, low
    uint32_t s = w & (w >> 1) & (w >> 2) & (w >> 3) & (w >> 4);
    return s ? hi(s) + 3 : -1;
}

uint32_t eval(const uint8_t *c, uint8_t n) {
    uint32_t sm[4] = {0, 0, 0, 0};
    uint8_t cnt[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (uint8_t i = 0; i < n; i++) {
        uint8_t r = (uint8_t)(c[i] >> 2);
        sm[c[i] & 3] |= 1u << r;
        cnt[r]++;
    }
    uint32_t all = sm[0] | sm[1] | sm[2] | sm[3];
    if (n >= 5) {
        // Seven cards can't hold a flush and quads or a full house at once,
        // so a flush is the answer unless it is a straight flush.
        for (uint8_t s = 0; s < 4; s++) {
            uint32_t m = sm[s];
            uint8_t k = 0;
            for (uint32_t t = m; t; t &= t - 1) k++;
            if (k < 5) continue;
            int st = straightHigh(m);
            if (st >= 0) return kick(((uint32_t)STRAIGHT_FLUSH << 4) | (uint32_t)st, 0, 4);
            return kick(FLUSH, m, 5);
        }
    }
    uint32_t quads = 0, trips = 0, pairs = 0;
    for (uint8_t r = 0; r < 13; r++) {
        if (cnt[r] == 4) quads |= 1u << r;
        else if (cnt[r] == 3) trips |= 1u << r;
        else if (cnt[r] == 2) pairs |= 1u << r;
    }
    if (quads) {
        uint8_t q = hi(quads);
        return kick(kick(((uint32_t)QUADS << 4) | q, all & ~(1u << q), 1), 0, 3);
    }
    if (trips) {
        uint8_t t = hi(trips);
        uint32_t rest = (trips & ~(1u << t)) | pairs;
        if (rest) return kick(((uint32_t)FULL_HOUSE << 8) | (uint32_t)(t << 4) | hi(rest), 0, 3);
    }
    if (n >= 5) {
        int st = straightHigh(all);
        if (st >= 0) return kick(((uint32_t)STRAIGHT << 4) | (uint32_t)st, 0, 4);
    }
    if (trips) {
        uint8_t t = hi(trips);
        return kick(kick(((uint32_t)TRIPS << 4) | t, all & ~(1u << t), 2), 0, 2);
    }
    if (pairs) {
        uint8_t p = hi(pairs);
        uint32_t rest = pairs & ~(1u << p);
        if (rest) {
            uint8_t q = hi(rest);
            uint32_t s = ((uint32_t)TWO_PAIR << 8) | (uint32_t)(p << 4) | q;
            return kick(kick(s, all & ~(1u << p) & ~(1u << q), 1), 0, 2);
        }
        return kick(kick(((uint32_t)PAIR << 4) | p, all & ~(1u << p), 3), 0, 1);
    }
    return kick(HIGH_CARD, all, 5);
}

uint32_t omaha(const uint8_t *h, const uint8_t *b, uint8_t nb, uint32_t beat) {
    static const uint8_t HP[6] = {0x01, 0x02, 0x03, 0x12, 0x13, 0x23};
    uint32_t best = 0;
    uint8_t five[5];
    for (uint8_t i = 0; i < nb; i++)
        for (uint8_t j = (uint8_t)(i + 1); j < nb; j++)
            for (uint8_t k = (uint8_t)(j + 1); k < nb; k++) {
                five[2] = b[i]; five[3] = b[j]; five[4] = b[k];
                for (uint8_t p = 0; p < 6; p++) {
                    five[0] = h[HP[p] >> 4];
                    five[1] = h[HP[p] & 15];
                    uint32_t s = eval(five, 5);
                    if (s > best) {
                        best = s;
                        if (best > beat) return best;
                    }
                }
            }
    return best;
}

uint32_t bestFive(const uint8_t *hole, uint8_t nh, const uint8_t *board, uint8_t nb, bool exactTwo,
                  uint8_t &holeMask, uint8_t &boardMask) {
    uint8_t all[12], n = 0;
    for (uint8_t i = 0; i < nh; i++) all[n++] = hole[i];
    for (uint8_t i = 0; i < nb; i++) all[n++] = board[i];
    uint32_t best = 0;
    uint16_t bestM = 0;
    holeMask = boardMask = 0;
    if (n <= 5) {                                   // nothing to choose
        best = eval(all, n);
        bestM = (uint16_t)((1u << n) - 1);
    } else {
        for (uint16_t m = 0; m < (1u << n); m++) {
            uint8_t k = 0, kh = 0, five[5];
            for (uint8_t i = 0; i < n; i++)
                if ((m >> i) & 1) {
                    if (k < 5) five[k] = all[i];
                    k++;
                    if (i < nh) kh++;
                }
            if (k != 5 || (exactTwo && kh != 2)) continue;
            uint32_t s = eval(five, 5);
            if (s > best) { best = s; bestM = m; }
        }
    }
    holeMask = (uint8_t)(bestM & ((1u << nh) - 1));
    boardMask = (uint8_t)(bestM >> nh);
    return best;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
static const char *const RANK_WORD[13] = {"TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT",
                                          "NINE", "TEN", "JACK", "QUEEN", "KING", "ACE"};
static const char *const CAT_NAME[CATS] = {"HIGH CARD", "PAIR", "TWO PAIR", "THREE OF A KIND", "STRAIGHT",
                                           "FLUSH", "FULL HOUSE", "FOUR OF A KIND", "STRAIGHT FLUSH"};

const char *catName(uint8_t c) { return c < CATS ? CAT_NAME[c] : ""; }

static char *put(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

static char *plural(char *p, uint8_t r) {
    p = put(p, RANK_WORD[r]);
    return put(p, r == R6 ? "ES" : "S");
}

char *describe(char *buf, uint32_t score) {
    uint8_t c = cat(score), r = topRank(score);
    char *p = buf;
    switch (c) {
        case HIGH_CARD: p = put(put(p, RANK_WORD[r]), " HIGH"); break;
        case PAIR: p = plural(put(p, "PAIR OF "), r); break;
        case TRIPS: p = plural(put(p, "THREE "), r); break;
        case QUADS: p = plural(put(p, "FOUR "), r); break;
        case STRAIGHT_FLUSH: p = put(p, r == RA ? "ROYAL FLUSH" : "STRAIGHT FLUSH"); break;
        default: p = put(p, CAT_NAME[c]); break;
    }
    return p;
}

}  // namespace hand
