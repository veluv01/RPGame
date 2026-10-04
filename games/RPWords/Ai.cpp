// The CPU's search (Ai.h says why it is shaped this way): pass one finds the
// letters each square beside a tile allows, pass two fits every word it knows
// everywhere it could go and keeps the best play.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
#include <string.h>
#include "Ai.h"
#include "Dict.h"

namespace ai {

using wd::SIZE;

static dict::Cursor cur;
static uint8_t side, level;
static uint8_t pass;                // 1: the cross-checks; 2: the placements; 3: done
static uint32_t nTried;
static uint16_t work;

static uint8_t rc[28];              // the rack by tile

// Each row (0..14) and column (15..29): the squares taken, the empty ones
// a play could hook on to, the letters on it.
static uint16_t occ[30], anchor[30];
static uint32_t lineLetters[30], boardLetters;

// A cross-check: an empty square with tiles across the line a word would be
// played along. `mask` is the letters that make a word of them (bit =
// letter). Filled in by the first pass over the list, read by the second.
// They are made in key order (the way played, then the square), so one is
// found by bisection; the first pass takes them by length, a chain each.
struct Cross {
    uint32_t mask;
    uint8_t cell;
    uint8_t at;                     // where the square falls in the cross word
    uint8_t lenDown;                // the cross word's length; bit 7: the word played runs down
    uint8_t next;                   // the next of the same length (0xFF: none)
};
static const uint8_t MAX_CROSS = 120;
static Cross cross[MAX_CROSS];
static uint8_t nCross, byLen[16];

// The best play so far.
static int16_t bestValue;
static game::Play bestPlay;

static inline uint8_t lineBase(uint8_t line) { return line < SIZE ? (uint8_t)(line * SIZE) : (uint8_t)(line - SIZE); }
static inline uint8_t lineStep(uint8_t line) { return line < SIZE ? 1 : SIZE; }

// The weaker players know fewer words: a word's hash decides, so it is
// always the same words.
static bool knows(const uint8_t *w, uint8_t n) {
    if (level == HIGH_ROLLER || n <= 3) return true;
    if (n > (level == TOURIST ? 5 : 7)) return false;
    uint8_t h = 0;
    for (uint8_t i = 0; i < n; i++) h = (uint8_t)(h * 5 + w[i]);
    return level == TOURIST ? h % 3 == 0 : h % 4 != 0;
}

static void survey() {
    memset(rc, 0, sizeof rc);
    for (uint8_t i = 0; i < wd::RACK; i++) rc[game::rack[side][i]]++;
    rc[0] = 0;
    boardLetters = 0;
    nCross = 0;
    memset(byLen, 0xFF, sizeof byLen);
    const uint8_t *b = game::board;
    for (uint8_t line = 0; line < 30; line++) {
        uint8_t base = lineBase(line), step = lineStep(line), other = step == 1 ? SIZE : 1;
        uint16_t o = 0, a = 0;
        uint32_t l = 0;
        for (uint8_t i = 0; i < SIZE; i++) {
            uint8_t cell = (uint8_t)(base + i * step), v = b[cell];
            if (v) { o |= (uint16_t)(1u << i); l |= 1ul << (v & wd::LETTER); continue; }
            // Empty: the tiles beside it across this line, either way.
            uint8_t before = 0, after = 0;
            for (uint8_t c = cell; (other == 1 ? c % SIZE != 0 : c >= SIZE) && b[c - other]; c = (uint8_t)(c - other)) before++;
            for (uint8_t c = cell; (other == 1 ? c % SIZE != SIZE - 1 : c < wd::CELLS - SIZE) && b[c + other]; c = (uint8_t)(c + other)) after++;
            if (!before && !after) continue;
            a |= (uint16_t)(1u << i);
            if (nCross == MAX_CROSS) continue;          // (a very full board: no play through this square)
            uint8_t len = (uint8_t)(before + 1 + after);
            cross[nCross] = {0, cell, before, (uint8_t)(len | (step != 1 ? 0x80 : 0)), byLen[len]};
            byLen[len] = nCross++;
        }
        occ[line] = o;
        anchor[line] = a;
        lineLetters[line] = l;
        boardLetters |= l;
    }
    if (!b[wd::CENTRE]) anchor[7] = anchor[22] = 1u << 7;      // the opening play: through the centre
}

// First pass: a word that fits a cross-check's tiles allows its letter there.
static void crossWord(const uint8_t *w, uint8_t n) {
    const uint8_t *b = game::board;
    for (uint8_t i = byLen[n]; i != 0xFF; i = cross[i].next) {
        Cross &c = cross[i];
        uint8_t other = (c.lenDown & 0x80) ? 1 : SIZE;
        const uint8_t *p = b + c.cell - c.at * other;
        bool same = true;
        for (uint8_t k = 0; k < n && same; k++, p += other) same = k == c.at || (*p & wd::LETTER) == w[k];
        if (same) c.mask |= 1ul << w[c.at];
    }
}

// The letters that may go on `cell` in a word played along `step`.
static uint32_t crossMask(uint8_t cell, uint8_t step) {
    uint16_t key = (uint16_t)((step != 1 ? 256 : 0) | cell);
    uint8_t lo = 0, hi = nCross;
    while (lo < hi) {
        uint8_t mid = (uint8_t)((lo + hi) / 2);
        uint16_t k = (uint16_t)((cross[mid].lenDown & 0x80 ? 256 : 0) | cross[mid].cell);
        if (k == key) return cross[mid].mask;
        if (k < key) lo = (uint8_t)(mid + 1); else hi = mid;
    }
    return 0;
}

// What a play is worth to the player: its score, less a little for tiles
// better kept (the high roller only).
static int16_t worth(const game::Play &pl, int16_t score) {
    if (level != HIGH_ROLLER || pl.n == wd::RACK) return score;
    int16_t v = score;
    for (uint8_t i = 0; i < pl.n; i++) {
        if (pl.p[i].tile & wd::BLANK) v = (int16_t)(v - 12);
        else if (pl.p[i].tile == 19) v = (int16_t)(v - 4);     // S
    }
    return v;
}

// Word w at offset s of a line.
static void fit(const uint8_t *w, uint8_t n, uint8_t line, uint8_t s) {
    uint16_t o = occ[line];
    uint16_t span = (uint16_t)(((1u << n) - 1) << s);
    if ((s && (o >> (s - 1) & 1)) || (s + n < SIZE && (o >> (s + n) & 1))) return;    // runs on into a tile
    if (!(span & (o | anchor[line])) || (span & o) == span) return;                     // touches nothing; places nothing
    work += 2;
    uint8_t left[28];
    memcpy(left, rc, sizeof left);
    game::Play pl;
    pl.n = 0;
    uint8_t base = lineBase(line), step = lineStep(line);
    bool opening = !game::board[wd::CENTRE];
    for (uint8_t i = 0; i < n; i++) {
        uint8_t cell = (uint8_t)(base + (s + i) * step), v = game::board[cell], l = w[i];
        if (v) {
            if ((v & wd::LETTER) != l) return;
            continue;
        }
        uint8_t tile;
        if (left[l]) { left[l]--; tile = l; }
        else if (left[wd::BLANK_TILE]) { left[wd::BLANK_TILE]--; tile = (uint8_t)(l | wd::BLANK); }
        else return;
        if (!opening && (anchor[line] >> (s + i) & 1) && !(crossMask(cell, step) >> l & 1)) return;
        pl.p[pl.n++] = {cell, tile};
    }
    wd::Result r;
    work += 6;
    if (wd::check(game::board, pl.p, pl.n, r) != wd::OK) return;
    nTried++;
    int16_t v = worth(pl, r.score);
    if (v > bestValue) { bestValue = v; bestPlay = pl; }
}

// Second pass: everywhere the word could go.
static void placeWord(const uint8_t *w, uint8_t n) {
    // What the word needs that the rack has not got.
    uint8_t need[28], lack[SIZE], nLack = 0;
    memset(need, 0, sizeof need);
    for (uint8_t i = 0; i < n; i++)
        if (++need[w[i]] > rc[w[i]]) lack[nLack++] = w[i];
    uint8_t blanks = rc[wd::BLANK_TILE];
    if (nLack <= blanks) {
        // The rack can spell it all: anywhere it hooks on.
        for (uint8_t line = 0; line < 30; line++) {
            if (!anchor[line] && !occ[line]) continue;
            for (uint8_t s = 0; s + n <= SIZE; s++) fit(w, n, line, s);
        }
        return;
    }
    // It must run through letters on the board that the rack lacks: all of
    // them (but for what the blanks cover) on one line.
    if (nLack - blanks >= n) return;
    uint8_t off = 0;
    for (uint8_t j = 0; j < nLack; j++) off += !(boardLetters >> lack[j] & 1);
    if (off > blanks) return;
    for (uint8_t line = 0; line < 30; line++) {
        uint32_t here = lineLetters[line];
        off = 0;
        for (uint8_t j = 0; j < nLack; j++) off += !(here >> lack[j] & 1);
        if (off > blanks) continue;
        uint8_t base = lineBase(line), step = lineStep(line);
        uint16_t seen = 0;                  // offsets tried on this line
        for (uint8_t p = 0; p < SIZE; p++) {
            if (!(occ[line] >> p & 1)) continue;
            uint8_t l = game::board[base + p * step] & wd::LETTER;
            bool lacked = false;
            for (uint8_t j = 0; j < nLack; j++) lacked |= lack[j] == l;
            if (!lacked) continue;
            for (uint8_t i = 0; i < n && i <= p; i++) {
                if (w[i] != l) continue;
                uint8_t s = (uint8_t)(p - i);
                if (s + n > SIZE || (seen >> s & 1)) continue;
                seen |= (uint16_t)(1u << s);
                fit(w, n, line, s);
            }
        }
    }
}

void start(uint8_t s, uint8_t lv) {
    side = s;
    level = lv;
    nTried = 0;
    bestValue = -32768;
    bestPlay.n = 0;
    survey();
    pass = nCross ? 1 : 2;
    dict::rewind(cur);
}

bool step(uint16_t budget) {
    uint8_t w[16], n;
    work = 0;
    while (pass < 3 && work < budget) {
        if (!dict::next(cur, w, n)) {
            pass = pass == 1 ? 2 : 3;
            dict::rewind(cur);
            continue;
        }
        work++;
        if (pass == 1) crossWord(w, n);
        else if (knows(w, n)) placeWord(w, n);
    }
    return pass == 3;
}

void stop() { pass = 3; }

uint16_t progress() {
    if (pass == 3) return 256;
    uint16_t p = dict::progress(cur);
    return nCross ? (uint16_t)((p + (pass == 2 ? 256 : 0)) / 2) : p;
}

uint32_t tried() { return nTried; }

bool chosen(game::Play &pl, wd::Result &r, bool hint) {
    if (!bestPlay.n) return false;
    // A poor play with tiles to draw: the high roller would rather swap.
    if (!hint && level == HIGH_ROLLER && bestValue < 6 && game::canSwap()) return false;
    pl = bestPlay;
    return wd::check(game::board, pl.p, pl.n, r) == wd::OK;
}

uint8_t swapMask() {
    if (!game::canSwap()) return 0;
    uint8_t m = 0;
    for (uint8_t i = 0; i < wd::RACK; i++) {
        uint8_t t = game::rack[side][i];
        // Keep a blank and an S; throw the rest back.
        if (t && t != wd::BLANK_TILE && t != 19) m |= (uint8_t)(1u << i);
    }
    return m ? m : 0x7F;
}

}  // namespace ai
