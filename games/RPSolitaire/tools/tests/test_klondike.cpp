// Host tests for Klondike.cpp: rpgame test
#include <stdio.h>
#include <string.h>
#include "../../Klondike.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long x_ = (long long)(a), y_ = (long long)(b); \
    if (x_ != y_) { failures++; printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, x_, y_); } } while (0)

static uint32_t rs = 12345;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static const Options STD3 = {0, STANDARD, 1, 0, 0, 0, {0, 0}};
static const Options STD1 = {1, STANDARD, 1, 0, 0, 0, {0, 0}};
static const Options VEG3 = {0, VEGAS, 1, 0, 0, 0, {0, 0}};
static const Options VEG1 = {1, VEGAS, 1, 0, 0, 0, {0, 0}};
static const Options TIMED = {0, STANDARD, 0, 0, 0, 0, {0, 0}};

// Every card exactly once; face-up runs alternate and descend; piles in range.
static bool sound(const Klondike &k) {
    uint8_t seen[52] = {0};
    if (k.nStock + k.nWaste > 24) return false;
    for (uint8_t p = 0; p < PILES; p++)
        for (uint8_t i = 0; i < k.count(p); i++) {
            uint8_t c = k.card(p, i);
            if (c >= 52 || seen[c]++) return false;
        }
    for (int c = 0; c < 52; c++) if (!seen[c]) return false;
    for (uint8_t col = 0; col < 7; col++) {
        if (k.nTab[col] > 19 || k.down[col] > k.nTab[col]) return false;
        if (k.nTab[col] && k.down[col] == k.nTab[col]) return false;      // the top card is always up
        for (uint8_t i = (uint8_t)(k.down[col] + 1); i < k.nTab[col]; i++) {
            uint8_t a = k.tab[col][i - 1], b = k.tab[col][i];
            if (redCard(a) == redCard(b) || rankOf(a) != rankOf(b) + 1) return false;
        }
    }
    return true;
}

static void testDeal() {
    Klondike k;
    k.deal(1, STD3);
    CHECK(sound(k));
    CHECK_EQ(k.nStock, 24);
    CHECK_EQ(k.nWaste, 0);
    for (int i = 0; i < 7; i++) { CHECK_EQ(k.nTab[i], i + 1); CHECK_EQ(k.down[i], i); }
    CHECK_EQ(k.score, 0);
    CHECK(k.live);
    Klondike b;
    b.deal(1, STD3);
    CHECK(memcmp(&k, &b, sizeof k) == 0);                   // same seed, same deal
    b.deal(2, STD3);
    CHECK(memcmp(&k, &b, sizeof k) != 0);
    k.deal(7, VEG3);
    CHECK_EQ(k.score, -52);
    CHECK(sizeof(Klondike) <= 200);
}

static void testDraw() {
    Klondike k;
    k.deal(3, STD3);
    uint8_t first = k.deck[0], third = k.deck[2];
    CHECK_EQ(k.draw(), 3);
    CHECK_EQ(k.nWaste, 3);
    CHECK_EQ(k.card(WASTE, 0), first);
    CHECK_EQ(k.top(WASTE), third);                          // the third card turned is on top
    CHECK_EQ(k.fan, 3);
    for (int i = 0; i < 7; i++) k.draw();
    CHECK_EQ(k.nStock, 0);
    CHECK_EQ(k.nWaste, 24);
    CHECK(sound(k));
    CHECK_EQ(k.draw(), 0xFF);                               // turned over
    CHECK_EQ(k.nStock, 24);
    CHECK_EQ(k.passes, 1);
    CHECK_EQ(k.card(STOCK, 23), first);                     // and comes round in the same order
    CHECK_EQ(k.draw(), 3);
    CHECK_EQ(k.top(WASTE), third);
    // An odd tail: 2 left in the stock deals 2.
    k.deal(3, STD3);
    k.nStock = 2;
    CHECK_EQ(k.draw(), 2);
    CHECK_EQ(k.fan, 2);
    // Draw one.
    k.deal(3, STD1);
    CHECK_EQ(k.draw(), 1);
    CHECK_EQ(k.top(WASTE), first);
}

static void testVegasPasses() {
    Klondike k;
    k.deal(5, VEG1);
    for (int i = 0; i < 24; i++) CHECK_EQ(k.draw(), 1);
    CHECK(!k.canRecycle());
    CHECK_EQ(k.draw(), 0);                                  // one pass only
    k.deal(5, VEG3);
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < 8; i++) CHECK_EQ(k.draw(), 3);
        if (pass < 2) CHECK_EQ(k.draw(), 0xFF);
    }
    CHECK_EQ(k.draw(), 0);                                  // three passes
    CHECK_EQ(k.score, -52);
}

static void testRecycleScore() {
    Klondike k;
    k.deal(9, STD1);
    k.score = 250;
    for (int i = 0; i < 24; i++) k.draw();
    k.draw();
    CHECK_EQ(k.score, 150);                                 // draw one: -100 a pass
    for (int i = 0; i < 24; i++) k.draw();
    k.draw();
    for (int i = 0; i < 24; i++) k.draw();
    k.draw();
    CHECK_EQ(k.score, 0);                                   // never below zero
    k.deal(9, STD3);
    k.score = 100;
    for (int pass = 1; pass <= 4; pass++) {
        for (int i = 0; i < 8; i++) k.draw();
        k.draw();
        CHECK_EQ(k.score, pass < 3 ? 100 : pass == 3 ? 80 : 60);   // draw three: -20 from the fourth pass
    }
}

// A hand-built table for the move rules.
static Klondike table() {
    Klondike k;
    k.deal(1, STD3);
    memset(k.nTab, 0, sizeof k.nTab);
    memset(k.down, 0, sizeof k.down);
    memset(k.found, 0, sizeof k.found);
    k.nStock = k.nWaste = k.fan = 0;
    return k;
}
static void put(Klondike &k, uint8_t col, uint8_t rank, uint8_t suit, bool faceDown = false) {
    k.tab[col][k.nTab[col]++] = makeCard(rank, suit);
    if (faceDown) k.down[col]++;
}

static void testMoves() {
    Klondike k = table();
    put(k, 0, 5, SPADES, true);
    put(k, 0, RK, HEARTS);                // col 0: [6S] KH
    put(k, 1, RQ, SPADES);                // col 1: QS
    put(k, 2, RQ, HEARTS);                // col 2: QH
    put(k, 3, RA, CLUBS);                 // col 3: AC
    put(k, 4, RJ, DIAMONDS);              // col 4: JD
    k.deck[0] = makeCard(1, CLUBS);       // waste: 2C
    k.nWaste = 1; k.fan = 1;
    // Colour and rank.
    CHECK(k.canMove(TAB + 1, 1, TAB + 0));                  // QS on KH
    CHECK(!k.canMove(TAB + 2, 1, TAB + 0));                 // QH on KH: same colour
    CHECK(!k.canMove(TAB + 3, 1, TAB + 0));                 // AC on KH: wrong rank
    CHECK(!k.canMove(TAB + 1, 1, TAB + 1));
    CHECK(!k.canMove(TAB + 1, 2, TAB + 0));                 // only one card there
    CHECK(!k.canMove(TAB + 0, 2, TAB + 6));                 // the 6S is face down
    // Kings only on an empty column.
    CHECK(k.canMove(TAB + 0, 1, TAB + 5));
    CHECK(!k.canMove(TAB + 1, 1, TAB + 5));
    // Foundations: aces first, then up in suit; any foundation stands for the right one.
    CHECK(k.canMove(TAB + 3, 1, FOUND + 0));
    CHECK(k.canMove(TAB + 3, 1, FOUND + 3));
    CHECK_EQ(k.dest(TAB + 3, FOUND + 3), FOUND + CLUBS);
    CHECK(!k.canMove(WASTE, 1, FOUND + 0));                 // 2C before the ace
    CHECK(!k.canMove(STOCK, 1, TAB + 0));
    CHECK(!k.canMove(TAB + 0, 1, WASTE));
    CHECK(!k.move(TAB + 2, 1, TAB + 0));
    // Scores (Standard).
    k.move(TAB + 3, 1, FOUND + 1);
    CHECK_EQ(k.found[CLUBS], 1);
    CHECK_EQ(k.score, 10);
    CHECK(k.canMove(WASTE, 1, FOUND + 0));
    k.move(WASTE, 1, FOUND + 0);
    CHECK_EQ(k.score, 20);
    CHECK_EQ(k.nWaste, 0);
    // Back down from the foundation: -15. 2C fits on nothing here, so build a spot.
    put(k, 3, 2, HEARTS);                                   // 3H
    CHECK(k.canMove(FOUND + CLUBS, 1, TAB + 3));
    CHECK(!k.canMove(FOUND + CLUBS, 1, FOUND + 1));
    k.move(FOUND + CLUBS, 1, TAB + 3);
    CHECK_EQ(k.score, 5);
    CHECK_EQ(k.found[CLUBS], 1);
    CHECK_EQ(k.top(TAB + 3), makeCard(1, CLUBS));
    // A run moves as one: QS, JD onto KH; taking the king off turns the 6S (+5).
    k.move(TAB + 4, 1, TAB + 1);                            // JD on QS
    CHECK_EQ(k.nTab[1], 2);
    CHECK(k.canMove(TAB + 1, 2, TAB + 0));
    CHECK(!k.canMove(TAB + 1, 1, TAB + 0));                 // the jack alone does not fit the king
    k.move(TAB + 1, 2, TAB + 0);
    CHECK_EQ(k.nTab[0], 4);
    CHECK_EQ(k.nTab[1], 0);
    CHECK_EQ(k.score, 5);
    CHECK(k.move(TAB + 0, 3, TAB + 1));                     // K Q J to the empty column: the 6S turns
    CHECK_EQ(k.down[0], 0);
    CHECK_EQ(k.score, 10);
    // Waste to tableau: +5.
    k.deck[0] = makeCard(9, CLUBS);                         // 10C on JD
    k.nWaste = 1;
    k.move(WASTE, 1, TAB + 1);
    CHECK_EQ(k.score, 15);
    CHECK_EQ(k.moves, 7);
}

static void testVegasScore() {
    Klondike k = table();
    k.scoring = VEGAS;
    k.score = -52;
    put(k, 0, RA, HEARTS);
    put(k, 1, 1, CLUBS, true);
    put(k, 1, 1, SPADES);
    put(k, 2, 2, DIAMONDS);
    k.move(TAB + 0, 1, FOUND);
    CHECK_EQ(k.score, -47);
    k.move(TAB + 1, 1, TAB + 2);                            // turns a card: nothing in Vegas
    CHECK_EQ(k.score, -47);
    k.scoring = NO_SCORE;
    k.move(TAB + 2, 1, TAB + 0);
    CHECK_EQ(k.score, -47);
}

static void testClock() {
    Klondike k;
    k.deal(4, TIMED);
    k.score = 50;
    for (int i = 0; i < 600; i++) k.tick();
    CHECK_EQ(k.secs, 0);                                    // the clock waits for the first move
    k.draw();
    for (int i = 0; i < 600; i++) k.tick();
    CHECK_EQ(k.secs, 10);
    CHECK_EQ(k.score, 48);                                  // -2 every ten seconds
    k.secs = 100;
    CHECK_EQ(k.bonus(), 7000);
    k.secs = 29;
    CHECK_EQ(k.bonus(), 0);
    k.deal(4, STD3);
    k.draw();
    k.score = 50;
    for (int i = 0; i < 1200; i++) k.tick();
    CHECK_EQ(k.score, 50);                                  // untimed: no penalty
    CHECK_EQ(k.secs, 20);
    CHECK_EQ(k.bonus(), 0);
}

static void testFinish() {
    Klondike k;
    k.deal(1, STD3);
    k.nearWin(10);
    CHECK(sound(k));
    CHECK(k.autoReady());
    CHECK(!k.won());
    int n = 0;
    while (!k.won() && n < 60) {
        uint8_t p = k.autoSource();
        CHECK(p != NO_PILE);
        if (p == NO_PILE) break;
        CHECK(k.canMove(p, 1, FOUND));
        k.move(p, 1, FOUND);
        n++;
    }
    CHECK_EQ(n, 10);
    CHECK(k.won());
    CHECK(!k.live);
    CHECK(!k.autoReady());
    CHECK_EQ(k.score, 100);
    k.deal(1, STD3);
    k.nearWin(52);
    CHECK(sound(k));
    CHECK_EQ(k.nTab[0], 13);
    n = 0;
    while (!k.won() && n < 60) { k.move(k.autoSource(), 1, FOUND); n++; }
    CHECK_EQ(n, 52);
}

// Random legal play: the table stays sound, and a simple player (foundation
// first, then turning cards, then the stock) wins a believable share.
static int playOut(uint32_t seed, const Options &o, bool greedy) {
    Klondike k;
    k.deal(seed, o);
    int idle = 0;
    for (int step = 0; step < 600 && !k.won(); step++) {
        bool moved = false;
        if (greedy) {
            uint8_t p = k.autoSource();
            if (p != NO_PILE) { k.move(p, 1, FOUND); moved = true; }
            // Move a whole face-up run if it turns a card or empties onto a king spot usefully.
            for (uint8_t c = 0; c < 7 && !moved; c++) {
                uint8_t n = k.faceUp(c);
                if (!n || (!k.down[c] && rankOf(k.tab[c][0]) == RK)) continue;
                for (uint8_t d = 0; d < 7 && !moved; d++)
                    if (k.canMove(TAB + c, n, TAB + d)) { k.move(TAB + c, n, TAB + d); moved = true; }
            }
            for (uint8_t d = 0; d < 7 && !moved; d++)
                if (k.canMove(WASTE, 1, TAB + d)) { k.move(WASTE, 1, TAB + d); moved = true; }
        } else {
            for (int tries = 0; tries < 30 && !moved; tries++) {
                uint8_t s = (uint8_t)(rnd() % PILES), d = (uint8_t)(rnd() % PILES), n = (uint8_t)(1 + rnd() % 4);
                if (k.canMove(s, n, d)) {
                    uint8_t to = k.dest(s, d), before = k.count(to);
                    k.move(s, n, d);
                    CHECK_EQ(k.count(to), before + n);
                    moved = true;
                }
            }
        }
        if (moved) idle = 0;
        else {
            if (!k.draw()) break;
            if (++idle > 40) break;
        }
        if (!sound(k)) { CHECK(!"table unsound"); return 0; }
        k.tick();
    }
    if (k.won()) CHECK(!k.live);
    return k.won();
}

static void testFuzz() {
    int wins = 0;
    for (uint32_t s = 1; s <= 1500; s++) {
        playOut(s, STD3, false);
        playOut(s * 7919, VEG1, false);
    }
    for (uint32_t s = 1; s <= 2000; s++) wins += playOut(s, STD1, true);
    printf("greedy draw-one player: %d wins of 2000\n", wins);
    CHECK(wins > 40);
    CHECK(wins < 1200);
}

int main() {
    testDeal();
    testDraw();
    testVegasPasses();
    testRecycleScore();
    testMoves();
    testVegasScore();
    testClock();
    testFinish();
    testFuzz();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
