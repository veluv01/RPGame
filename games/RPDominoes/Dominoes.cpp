// The rules of DRAW and ALL FIVES (Dominoes.h): the deal, where a tile may
// go, what it scores, when a round ends and what it is worth.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <string.h>
#include "Dominoes.h"

namespace dom {

const uint8_t PIPS[TILES] = {
    0x00,
    0x10, 0x11,
    0x20, 0x21, 0x22,
    0x30, 0x31, 0x32, 0x33,
    0x40, 0x41, 0x42, 0x43, 0x44,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
};

uint8_t count(uint32_t m) {
    uint8_t n = 0;
    for (; m; m &= m - 1) n++;
    return n;
}

uint8_t handPips(uint32_t m) {
    uint8_t n = 0;
    for (uint8_t t = 0; t < TILES; t++) if ((m >> t) & 1) n = (uint8_t)(n + pips(t));
    return n;
}

uint8_t nth(uint32_t m, uint8_t k) {
    for (uint8_t t = 0; t < TILES; t++)
        if (((m >> t) & 1) && !k--) return t;
    return NONE;
}

void Rng::seed(uint32_t s, uint32_t stream) {
    state = (s ^ (stream * 0x9E3779B9u)) * 2654435761u;
    if (!state) state = 0x1234567u;
    for (uint8_t i = 0; i < 4; i++) next();
}

uint32_t Rng::next() {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

uint8_t Rng::below(uint8_t n) { return (uint8_t)(((next() >> 16) * n) >> 16); }

void shuffle(uint8_t *order, Rng &rng) {
    for (uint8_t i = 0; i < TILES; i++) order[i] = i;
    for (uint8_t i = TILES - 1; i > 0; i--) {
        uint8_t j = rng.below((uint8_t)(i + 1)), t = order[i];
        order[i] = order[j]; order[j] = t;
    }
}

void deal(Round &r, uint8_t game, const uint8_t *order, uint8_t first) {
    memset(&r, 0, sizeof r);
    r.game = game;
    r.turn = first;
    for (uint8_t i = 0; i < TILES; i++) {
        uint32_t bit = 1u << order[i];
        if (i < 7) r.hand[0] |= bit;
        else if (i < 14) r.hand[1] |= bit;
        else r.bone |= bit;
    }
}

static bool sidesOpen(const Round &r) { return r.spinner && r.len[E] && r.len[W]; }

uint8_t armsFor(const Round &r, uint8_t tile) {
    if (!r.n) return 1;
    uint8_t m = 0, top = sidesOpen(r) ? ARMS : N;
    for (uint8_t a = 0; a < top; a++)
        if (lo(tile) == r.end[a] || hi(tile) == r.end[a]) m |= (uint8_t)(1 << a);
    return m;
}

bool canPlay(const Round &r, uint8_t side) {
    for (uint8_t t = 0; t < TILES; t++)
        if (((r.hand[side] >> t) & 1) && armsFor(r, t)) return true;
    return false;
}

uint8_t endSum(const Round &r) {
    if (!r.n) return 0;
    uint8_t first = r.play[0] & 31, sum = 0;
    bool firstDbl = isDouble(first);
    // A double set first shows both its halves until it is covered on both sides.
    if (firstDbl && !(r.len[E] && r.len[W])) sum = (uint8_t)(2 * lo(first));
    for (uint8_t a = 0; a < ARMS; a++) {
        if (!r.len[a]) {
            if (a < N && !firstDbl) sum = (uint8_t)(sum + r.end[a]);
            continue;
        }
        sum = (uint8_t)(sum + (((r.dbl >> a) & 1) ? 2 * r.end[a] : r.end[a]));
    }
    return sum;
}

uint8_t place(Round &r, uint8_t side, uint8_t tile, uint8_t arm) {
    r.hand[side] &= ~(1u << tile);
    if (!r.n) {
        r.end[E] = r.end[N] = r.end[S] = hi(tile);
        r.end[W] = lo(tile);
        r.spinner = r.game == FIVES && isDouble(tile);
        r.play[0] = tile;
    } else {
        r.end[arm] = lo(tile) == r.end[arm] ? hi(tile) : lo(tile);
        r.len[arm]++;
        r.dbl = (uint8_t)((r.dbl & ~(1 << arm)) | (isDouble(tile) << arm));
        r.play[r.n] = (uint8_t)(tile | (arm << 5));
    }
    r.n++;
    r.passes = 0;
    r.turn = side ^ 1;
    uint8_t sum = endSum(r);
    return r.game == FIVES && sum % 5 == 0 ? sum : 0;
}

// The pips the side to move has just shown it lacks: every open end's.
static uint8_t openValues(const Round &r) {
    uint8_t m = 0, top = sidesOpen(r) ? ARMS : N;
    for (uint8_t a = 0; a < top; a++) m |= (uint8_t)(1 << r.end[a]);
    return m;
}

void drew(Round &r, uint8_t side, uint8_t tile) {
    r.voids[side] |= openValues(r);
    r.bone &= ~(1u << tile);
    r.hand[side] |= 1u << tile;
}

void passed(Round &r, uint8_t side) {
    r.voids[side] = openValues(r);
    r.passes++;
    r.turn = side ^ 1;
}

uint8_t options(const Round &r, uint8_t tile, uint8_t *arms) {
    uint8_t mask = armsFor(r, tile), n = 0;
    uint32_t sig[ARMS];
    for (uint8_t a = 0; a < ARMS; a++) {
        if (!((mask >> a) & 1)) continue;
        Round c = r;
        uint8_t pts = place(c, 0, tile, a);
        // What it scores, what the ends then add up to, and which pips are open.
        uint32_t s = pts | ((uint32_t)endSum(c) << 8) | ((uint32_t)openValues(c) << 16);
        uint8_t k = 0;
        while (k < n && sig[k] != s) k++;
        if (k == n) { sig[n] = s; arms[n++] = a; }
        else if (r.len[a] < r.len[arms[k]]) arms[k] = a;
    }
    return n;
}

bool over(const Round &r, uint8_t &reason) {
    if (r.n && (!r.hand[0] || !r.hand[1])) { reason = DOMINO; return true; }
    if (r.passes >= 2) { reason = BLOCKED; return true; }
    return false;
}

uint8_t settle(const Round &r, uint8_t reason, uint8_t &winner) {
    uint8_t p0 = handPips(r.hand[0]), p1 = handPips(r.hand[1]), pts;
    if (reason == DOMINO) {
        winner = r.hand[0] ? 1 : 0;
        pts = winner ? p0 : p1;
    } else {
        winner = p0 == p1 ? 2 : p0 < p1 ? 0 : 1;
        pts = (uint8_t)(p0 < p1 ? p1 - p0 : p0 - p1);
    }
    if (r.game == FIVES) pts = (uint8_t)((pts + 2) / 5 * 5);
    return pts;
}

uint8_t opener(const Round &r, uint8_t &tile) {
    // Doubles outrank everything; among doubles, among the rest, the pips.
    uint8_t best = 0, side = 0;
    tile = NONE;
    for (uint8_t s = 0; s < 2; s++)
        for (uint8_t t = 0; t < TILES; t++) {
            if (!((r.hand[s] >> t) & 1)) continue;
            uint8_t rank = (uint8_t)(1 + pips(t) + (isDouble(t) ? 32 : 0));
            if (rank > best || (rank == best && t > tile)) { best = rank; tile = t; side = s; }
        }
    return side;
}

bool rebuild(Round &r) {
    uint32_t played = 0;
    Round c;
    memset(&c, 0, sizeof c);
    c.game = r.game;
    if (r.n > TILES || r.turn > 1 || r.game > FIVES) return false;
    for (uint8_t i = 0; i < r.n; i++) {
        uint8_t t = r.play[i] & 31, a = r.play[i] >> 5;
        if (t >= TILES || a >= ARMS || ((played >> t) & 1)) return false;
        if (!((armsFor(c, t) >> a) & 1)) return false;
        place(c, 0, t, a);
        played |= 1u << t;
    }
    if ((r.hand[0] & r.hand[1]) || ((r.hand[0] | r.hand[1]) & (r.bone | played)) || (r.bone & played) ||
        (r.hand[0] | r.hand[1] | r.bone | played) != ALL)
        return false;
    memcpy(r.end, c.end, sizeof r.end);
    memcpy(r.len, c.len, sizeof r.len);
    r.dbl = c.dbl;
    r.spinner = c.spinner;
    return true;
}

}  // namespace dom
