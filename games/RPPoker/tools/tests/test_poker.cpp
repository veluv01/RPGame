// Host tests for CHPoker's rules: the hand evaluator, the table (betting,
// pots, every variant) and the CPU players. Built by rpgame test.
//
//   test_poker [--long]      --long also enumerates all 133,784,560 seven-card hands
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include "../../Cards.h"
#include "../../Hand.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

static uint8_t parseCard(const char *s) {
    static const char R[] = "23456789TJQKA", S[] = "cdhs";
    return makeCard((uint8_t)(strchr(R, s[0]) - R), (uint8_t)(strchr(S, s[1]) - S));
}
// "Ah Kd 5c" -> cards
static uint8_t parse(const char *s, uint8_t *out) {
    uint8_t n = 0;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        out[n++] = parseCard(s);
        s += 2;
    }
    return n;
}
static uint32_t ev(const char *s) { uint8_t c[8]; uint8_t n = parse(s, c); return hand::eval(c, n); }

// ---------------------------------------------------------------------------
static void testFiveCardHands() {
    static const uint32_t EXPECT[hand::CATS] = {1302540, 1098240, 123552, 54912, 10200, 5108, 3744, 624, 40};
    uint32_t count[hand::CATS] = {0};
    std::vector<uint8_t> seen((9u << 20) / 8 + 1, 0);
    uint32_t distinct = 0, royals = 0;
    uint8_t c[5];
    for (c[0] = 0; c[0] < 52; c[0]++)
        for (c[1] = c[0] + 1; c[1] < 52; c[1]++)
            for (c[2] = c[1] + 1; c[2] < 52; c[2]++)
                for (c[3] = c[2] + 1; c[3] < 52; c[3]++)
                    for (c[4] = c[3] + 1; c[4] < 52; c[4]++) {
                        uint32_t s = hand::eval(c, 5);
                        uint8_t k = hand::cat(s);
                        if (k >= hand::CATS) { CHECK(false, "bad category %u", k); continue; }
                        count[k]++;
                        if (k == hand::STRAIGHT_FLUSH && hand::topRank(s) == RA) royals++;
                        if (!(seen[s >> 3] & (1 << (s & 7)))) { seen[s >> 3] |= (uint8_t)(1 << (s & 7)); distinct++; }
                    }
    for (int k = 0; k < hand::CATS; k++)
        CHECK(count[k] == EXPECT[k], "5-card %s: %u, expected %u", hand::catName((uint8_t)k), count[k], EXPECT[k]);
    CHECK(distinct == 7462, "distinct 5-card scores %u, expected 7462", distinct);
    CHECK(royals == 4, "royal flushes %u", royals);
}

static void testOrdering() {
    // Each line beats the next.
    static const char *const LADDER[] = {
        "As Ks Qs Js Ts", "Ks Qs Js Ts 9s", "5d 4d 3d 2d Ad", "Ac Ad Ah As 2c", "Kc Kd Kh Ks Ac",
        "Ac Ad Ah Kc Kd", "Ac Ad Ah 2c 2d", "Kc Kd Kh Ac Ad", "Ah Jh 9h 7h 5h", "Ah Jh 9h 7h 4h",
        "Ac Kd Qh Js Tc", "6c 5d 4h 3s 2c", "5c 4d 3h 2s Ac", "Ac Ad Ah Kc Qd", "Ac Ad Ah Kc Jd",
        "Kc Kd Kh As Qd", "Ac Ad Kc Kd Qh", "Ac Ad Kc Kd Jh", "Ac Ad Qc Qd Kh", "Kc Kd Qc Qd Ah",
        "Ac Ad Kc Qd Jh", "Ac Ad Kc Qd Th", "Kc Kd Ac Qd Jh", "Ac Kd Qh Js 9c", "Ac Kd Qh Js 8c",
        "Kc Qd Jh Ts 8c", "7c 5d 4h 3s 2c",
    };
    int n = (int)(sizeof LADDER / sizeof LADDER[0]);
    for (int i = 0; i + 1 < n; i++)
        CHECK(ev(LADDER[i]) > ev(LADDER[i + 1]), "%s should beat %s", LADDER[i], LADDER[i + 1]);
    CHECK(ev("Ac Kd Qh Js 9c") == ev("Ad Kh Qs Jc 9d"), "suits don't matter");
    // Seven cards: the best five, kickers from the rest.
    CHECK(ev("Ac Ad Kc Kd Qc Qd 2h") == ev("Ac Ad Kc Kd Qh"), "three pairs: best two + the third as kicker");
    CHECK(ev("Ac Ad Ah Kc Kd Kh 2s") == ev("Ac Ad Ah Kc Kd"), "two trips: aces full of kings");
    CHECK(hand::cat(ev("2h 3h 4h 5h 7h 6c 8d")) == hand::FLUSH, "flush beats the straight beside it");
    CHECK(hand::cat(ev("2h 3h 4h 5h 6h 7c 8d")) == hand::STRAIGHT_FLUSH, "straight flush");
    CHECK(hand::cat(ev("Ah 2c 3d 4s 5h 9c Kd")) == hand::STRAIGHT && hand::topRank(ev("Ah 2c 3d 4s 5h 9c Kd")) == R5,
          "the wheel is five high");
    CHECK(ev("Ah 2c 3d 4s 5h 6c Kd") > ev("Ah 2c 3d 4s 5h 9c Kd"), "six high beats the wheel");
    // Fewer than five cards (stud's showing hands): no straights or flushes.
    CHECK(hand::cat(ev("2h 5h 9h Kh")) == hand::HIGH_CARD, "four hearts showing are just king high");
    CHECK(hand::cat(ev("4c 5d 6h 7s")) == hand::HIGH_CARD, "four to a straight showing is high card");
    CHECK(ev("Kc Kd") > ev("Ac Qd"), "a pair showing beats ace high");
    CHECK(ev("Kc Kd 3h") > ev("Kh Ks 2c"), "kickers count with three cards");
    CHECK(hand::cat(ev("9c 9d 9h")) == hand::TRIPS && hand::cat(ev("9c 9d 9h 9s")) == hand::QUADS, "trips, quads showing");
    char buf[24];
    hand::describe(buf, ev("As Ks Qs Js Ts"));
    CHECK(!strcmp(buf, "ROYAL FLUSH"), "describe: %s", buf);
    hand::describe(buf, ev("6c 6d Ah 3s 2c"));
    CHECK(!strcmp(buf, "PAIR OF SIXES"), "describe: %s", buf);
    hand::describe(buf, ev("Ac 9d 6h 3s 2c"));
    CHECK(!strcmp(buf, "ACE HIGH"), "describe: %s", buf);
}

static void deal(uint8_t *out, int n) {
    uint8_t deck[52];
    for (int i = 0; i < 52; i++) deck[i] = (uint8_t)i;
    for (int i = 0; i < n; i++) {
        int j = i + (int)(rnd() % (uint32_t)(52 - i));
        uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
        out[i] = deck[i];
    }
}

static void testSevenCards(bool full) {
    // eval(7) is the best of its 21 five-card hands.
    for (int t = 0; t < 1000000; t++) {
        uint8_t c[7];
        deal(c, 7);
        uint32_t best = 0;
        for (int a = 0; a < 7; a++)
            for (int b = a + 1; b < 7; b++) {
                uint8_t f[5], k = 0;
                for (int i = 0; i < 7; i++) if (i != a && i != b) f[k++] = c[i];
                uint32_t s = hand::eval(f, 5);
                if (s > best) best = s;
            }
        uint32_t s7 = hand::eval(c, 7);
        if (s7 != best) { CHECK(false, "eval7 %08x != best of 21 %08x", s7, best); break; }
        uint8_t hm, bm;
        uint32_t b5 = hand::bestFive(c, 2, c + 2, 5, false, hm, bm);
        uint8_t f[5], k = 0;
        for (int i = 0; i < 2; i++) if ((hm >> i) & 1) f[k++] = c[i];
        for (int i = 0; i < 5; i++) if ((bm >> i) & 1) f[k++] = c[2 + i];
        if (b5 != s7 || k != 5 || hand::eval(f, 5) != s7) { CHECK(false, "bestFive masks"); break; }
    }
    if (!full) return;
    static const uint64_t EXPECT[hand::CATS] = {23294460ull, 58627800ull, 31433400ull, 6461620ull, 6180020ull,
                                                4047644ull, 3473184ull, 224848ull, 41584ull};
    uint64_t count[hand::CATS] = {0};
    uint8_t c[7];
    for (c[0] = 0; c[0] < 52; c[0]++)
     for (c[1] = c[0] + 1; c[1] < 52; c[1]++)
      for (c[2] = c[1] + 1; c[2] < 52; c[2]++)
       for (c[3] = c[2] + 1; c[3] < 52; c[3]++)
        for (c[4] = c[3] + 1; c[4] < 52; c[4]++)
         for (c[5] = c[4] + 1; c[5] < 52; c[5]++)
          for (c[6] = c[5] + 1; c[6] < 52; c[6]++) count[hand::cat(hand::eval(c, 7))]++;
    for (int k = 0; k < hand::CATS; k++)
        CHECK(count[k] == EXPECT[k], "7-card %s: %llu, expected %llu", hand::catName((uint8_t)k),
              (unsigned long long)count[k], (unsigned long long)EXPECT[k]);
}

static void testOmaha() {
    uint8_t h[4], b[5];
    // A four-flush on the board and one heart in the hand is not a flush.
    parse("Ah Kc 7d 2s", h); parse("2h 5h 9h Jh Qc", b);
    CHECK(hand::cat(hand::omaha(h, b, 5)) != hand::FLUSH, "one suited hole card makes no flush");
    parse("Ah 3h 7d 2s", h);
    CHECK(hand::cat(hand::omaha(h, b, 5)) == hand::FLUSH, "two suited hole cards make the flush");
    // Board trips plus a pocket pair: a full house (3 board + 2 hole).
    parse("5c 5d 7h 9s", h); parse("Kc Kd Kh 2c 3d", b);
    CHECK(hand::cat(hand::omaha(h, b, 5)) == hand::FULL_HOUSE, "board trips + pocket pair");
    // Four of a rank in the hand is just a pair.
    parse("8c 8d 8h 8s", h); parse("Ac Kd 2h 5s 9c", b);
    CHECK(hand::cat(hand::omaha(h, b, 5)) == hand::PAIR, "quads in the hand play as a pair");
    // One hole card to a board straight: no straight.
    parse("Tc 2d 2h 3s", h); parse("6c 7d 8h 9s Kc", b);
    CHECK(hand::cat(hand::omaha(h, b, 5)) == hand::PAIR, "one card to a board straight is not a straight");
    // Random deals agree with the brute force (exactly two from the hand).
    for (int t = 0; t < 200000; t++) {
        uint8_t c[9];
        deal(c, 9);
        uint8_t hm, bm;
        uint32_t a = hand::omaha(c, c + 4, 5);
        uint32_t s = hand::bestFive(c, 4, c + 4, 5, true, hm, bm);
        int nh = __builtin_popcount(hm), nb = __builtin_popcount(bm);
        if (a != s || nh != 2 || nb != 3) { CHECK(false, "omaha %08x vs brute %08x (%d+%d)", a, s, nh, nb); break; }
        // The early exit only promises "better than beat".
        uint32_t beat = s - 1;
        if (hand::omaha(c, c + 4, 5, beat) <= beat) { CHECK(false, "omaha early exit"); break; }
    }
}

// ---------------------------------------------------------------------------
void testTable();
void testAi();

int main(int argc, char **argv) {
    bool full = argc > 1 && !strcmp(argv[1], "--long");
    testFiveCardHands();
    testOrdering();
    testSevenCards(full);
    testOmaha();
    testTable();
    testAi();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

int testFailures() { return failures; }
void testCheck(bool ok, const char *what) {
    checks++;
    if (!ok) { failures++; printf("FAIL %s\n", what); }
}
