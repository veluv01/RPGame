// Klondike's rules and the Windows scoring (see Klondike.h).
#pragma GCC optimize("Os")
#include <string.h>
#include "Klondike.h"

static void addScore(Klondike &k, int d) {
    if (k.scoring == NO_SCORE) return;
    k.score += d;
    if (k.scoring == STANDARD && k.score < 0) k.score = 0;
}

void Klondike::deal(uint32_t seed, const Options &o) {
    memset(this, 0, sizeof *this);
    uint8_t d[52];
    for (uint8_t i = 0; i < 52; i++) d[i] = i;
    uint32_t s = seed ? seed : 0x9E3779B9u;
    for (uint8_t i = 51; i > 0; i--) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        uint8_t j = (uint8_t)(s % (uint32_t)(i + 1));
        uint8_t t = d[i]; d[i] = d[j]; d[j] = t;
    }
    uint8_t n = 0;
    for (uint8_t i = 0; i < 7; i++) {
        for (uint8_t j = 0; j <= i; j++) tab[i][j] = d[n++];
        nTab[i] = (uint8_t)(i + 1);
        down[i] = i;
    }
    memcpy(deck, d + 28, 24);
    nStock = 24;
    drawOne = o.draw;
    scoring = o.scoring;
    timed = o.timed == 0;
    live = 1;
    score = scoring == VEGAS ? -52 : 0;
}

// Vegas lets you through the stock once dealing one card, three times
// dealing three.
bool Klondike::canRecycle() const {
    if (!nWaste) return false;
    return scoring != VEGAS || (!drawOne && passes < 2);
}

uint8_t Klondike::draw() {
    if (nStock) {
        uint8_t n = drawOne ? 1 : 3;
        if (n > nStock) n = nStock;
        for (uint8_t i = 0; i < n; i++) { deck[nWaste++] = deck[24 - nStock]; nStock--; }
        fan = n;
        started = 1;
        moves++;
        return n;
    }
    if (!canRecycle()) return 0;
    memmove(deck + 24 - nWaste, deck, nWaste);
    nStock = nWaste;
    nWaste = 0;
    fan = 0;
    passes++;
    moves++;
    if (scoring == STANDARD) {
        if (drawOne) addScore(*this, -100);
        else if (passes >= 3) addScore(*this, -20);
    }
    return 0xFF;
}

uint8_t Klondike::count(uint8_t p) const {
    if (p == STOCK) return nStock;
    if (p == WASTE) return nWaste;
    if (p < TAB) return found[p - FOUND];
    return nTab[p - TAB];
}

uint8_t Klondike::card(uint8_t p, uint8_t i) const {
    if (p == STOCK) return deck[23 - i];
    if (p == WASTE) return deck[i];
    if (p < TAB) return makeCard(i, (uint8_t)(p - FOUND));
    return tab[p - TAB][i];
}

uint8_t Klondike::dest(uint8_t src, uint8_t dst) const {
    if (dst >= FOUND && dst < TAB) return (uint8_t)(FOUND + suitOf(top(src)));
    return dst;
}

bool Klondike::canMove(uint8_t src, uint8_t n, uint8_t dst) const {
    if (src == STOCK || src >= PILES || dst < FOUND || dst >= PILES || !n) return false;
    uint8_t have = src >= TAB ? faceUp((uint8_t)(src - TAB)) : count(src);
    if (n > have || (src < TAB && n != 1)) return false;
    uint8_t b = card(src, (uint8_t)(count(src) - n));         // the card that has to fit
    if (dst < TAB) return src >= TAB || src == WASTE ? n == 1 && found[suitOf(b)] == rankOf(b) : false;
    if (dst == src) return false;
    uint8_t t = top(dst);
    if (t == NO_CARD) return rankOf(b) == RK;
    return redCard(t) != redCard(b) && rankOf(t) == rankOf(b) + 1;
}

bool Klondike::move(uint8_t src, uint8_t n, uint8_t dst) {
    if (!canMove(src, n, dst)) return false;
    uint8_t c[13];
    bool turned = false;
    uint8_t from = (uint8_t)(count(src) - n);
    for (uint8_t i = 0; i < n; i++) c[i] = card(src, (uint8_t)(from + i));
    if (src == WASTE) {
        nWaste--;
        if (fan > 1) fan--;
    } else if (src < TAB) {
        found[src - FOUND]--;
    } else {
        uint8_t col = (uint8_t)(src - TAB);
        nTab[col] = from;
        if (from && down[col] == from) { down[col]--; turned = true; }
    }
    if (dst < TAB) {
        found[suitOf(c[0])]++;
        addScore(*this, scoring == VEGAS ? 5 : 10);
    } else {
        uint8_t col = (uint8_t)(dst - TAB);
        memcpy(tab[col] + nTab[col], c, n);
        nTab[col] = (uint8_t)(nTab[col] + n);
        if (src == WASTE) addScore(*this, scoring == VEGAS ? 0 : 5);
        else if (src < TAB) addScore(*this, scoring == VEGAS ? -5 : -15);
    }
    if (turned && scoring == STANDARD) addScore(*this, 5);
    started = 1;
    moves++;
    if (won()) live = 0;
    return turned;
}

bool Klondike::autoReady() const {
    if (nStock || nWaste || won()) return false;
    for (uint8_t i = 0; i < 7; i++) if (down[i]) return false;
    return true;
}

uint8_t Klondike::autoSource() const {
    uint8_t best = NO_PILE, rank = 99;
    for (uint8_t p = WASTE; p < PILES; p++) {
        if (p >= FOUND && p < TAB) continue;
        uint8_t c = top(p);
        if (c == NO_CARD || found[suitOf(c)] != rankOf(c) || rankOf(c) >= rank) continue;
        best = p;
        rank = rankOf(c);
    }
    return best;
}

void Klondike::tick() {
    if (!started || !live) return;
    if (++sub < 60) return;
    sub = 0;
    if (secs < 59999) secs++;
    if (timed && scoring == STANDARD && secs % 10 == 0) addScore(*this, -2);
}

int32_t Klondike::bonus() const {
    if (scoring != STANDARD || !timed || secs < 30) return 0;
    return 700000 / secs;
}

void Klondike::nearWin(uint8_t left) {
    // The last `left` cards (kings first, then queens...) wait on the tableau.
    nStock = nWaste = fan = 0;
    memset(nTab, 0, sizeof nTab);
    memset(down, 0, sizeof down);
    for (uint8_t s = 0; s < 4; s++) found[s] = 13;
    for (uint8_t i = 0; i < left && i < 52; i++) {
        // Rows of four: K K K K, then Q Q Q Q in the other colour on top.
        uint8_t s = (uint8_t)(i & 3), r = (uint8_t)(12 - (i >> 2));
        uint8_t suit = (uint8_t)((s + (i >> 2)) & 3);
        tab[s][nTab[s]++] = makeCard(r, suit);
        found[suit]--;
    }
    live = 1;
    started = 1;
}
