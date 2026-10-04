// Host tests for the rules (Yacht.cpp).
//
// 1. Every box against an independent oracle, for all 7,776 rolls.
// 2. The joker rule, the upper bonus, bonus yachts, turn order, the money.
// 3. The house player plays thousands of solo games: its score distribution
//    is what the paytable (Yacht::TIER_AT) is set against; the return on an
//    ante is printed and checked to stay in a casino's range.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "../../Yacht.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static int oracle(int cat, const uint8_t *d) {
    int s[5];
    for (int i = 0; i < 5; i++) s[i] = d[i];
    std::sort(s, s + 5);
    int sum = s[0] + s[1] + s[2] + s[3] + s[4];
    auto count = [&](int f) { int n = 0; for (int i = 0; i < 5; i++) n += s[i] == f; return n; };
    auto has = [&](int f) { return count(f) > 0; };
    switch (cat) {
        case KIND3: for (int f = 1; f <= 6; f++) if (count(f) >= 3) return sum; return 0;
        case KIND4: for (int f = 1; f <= 6; f++) if (count(f) >= 4) return sum; return 0;
        case FULL: return (s[0] == s[1] && s[3] == s[4] && s[0] != s[4] && (s[2] == s[1] || s[2] == s[3])) ? 25 : 0;
        case SMALL:
            for (int a = 1; a <= 3; a++) if (has(a) && has(a + 1) && has(a + 2) && has(a + 3)) return 30;
            return 0;
        case LARGE: return (s[1] == s[0] + 1 && s[2] == s[1] + 1 && s[3] == s[2] + 1 && s[4] == s[3] + 1) ? 40 : 0;
        case YACHT: return s[0] == s[4] ? 50 : 0;
        case CHANCE: return sum;
        default: return count(cat + 1) * (cat + 1);
    }
}

static void testBoxes() {
    Card empty;
    memset(&empty, 0, sizeof empty);
    for (int n = 0; n < 7776; n++) {
        uint8_t d[5];
        for (int i = 0, k = n; i < 5; i++, k /= 6) d[i] = (uint8_t)(1 + k % 6);
        for (int c = 0; c < CAT_COUNT; c++)
            CHECK(Yacht::pointsFor(empty, (uint8_t)c, d) == oracle(c, d), "box %d for %d%d%d%d%d: %d, oracle %d", c,
                  d[0], d[1], d[2], d[3], d[4], Yacht::pointsFor(empty, (uint8_t)c, d), oracle(c, d));
        uint8_t co = Yacht::combo(d);
        CHECK(co == CAT_COUNT || oracle(co, d) > 0, "combo %d for %d%d%d%d%d", co, d[0], d[1], d[2], d[3], d[4]);
    }
}

static Yacht g;

static void turn(const uint8_t *d, uint8_t cat, uint8_t wantEv, int wantPts) {
    g.force(d);
    g.roll();
    CHECK(g.legal(cat), "box %d should be legal", cat);
    uint8_t ev = g.score(cat);
    CHECK(ev == wantEv, "events %d, expected %d", ev, wantEv);
    CHECK(g.lastPts == wantPts, "points %d, expected %d", g.lastPts, wantPts);
}

static void testRules() {
    static const uint8_t Y4[5] = {4, 4, 4, 4, 4}, Y2[5] = {2, 2, 2, 2, 2}, JUNK[5] = {1, 2, 2, 5, 6};
    memset(&g, 0, sizeof g);
    g.seed(7);
    g.newPurse();
    g.opt.ante = 1;
    g.newGame(M_SOLO);
    CHECK(g.purse == 475 && g.ante == 25, "ante taken: purse %d ante %d", (int)g.purse, g.ante);
    CHECK(!g.legal(CHANCE), "nothing scores before the first roll");
    turn(Y4, YACHT, EV_YACHT, 50);
    CHECK(g.purse == 475 + 25, "yacht pays at once: %d", (int)g.purse);
    // A second yacht of fours: 100 extra, and it must go in FOURS.
    g.force(Y4);
    g.roll();
    CHECK(g.legal(FOURS) && !g.legal(CHANCE) && !g.legal(FULL), "joker: own upper box first");
    CHECK(g.score(FOURS) == EV_EXTRA && g.lastPts == 20, "bonus yacht in fours");
    CHECK(g.card[0].extra == 1 && g.card[0].total() == 170, "total %d", g.card[0].total());
    // A third, fours gone: any lower box, as a joker.
    g.force(Y4);
    g.roll();
    CHECK(!g.legal(ONES) && g.legal(LARGE) && g.points(LARGE) == 40 && g.points(FULL) == 25 && g.points(SMALL) == 30,
          "joker: lower boxes at full value");
    CHECK(g.score(LARGE) == EV_EXTRA && g.lastPts == 40, "joker large straight");
    // A yacht box scratched earns no bonus, but the joker still plays.
    Yacht h;
    memset(&h, 0, sizeof h);
    h.seed(9);
    h.newGame(M_PARTY2);
    CHECK(h.ante == 0 && h.purse == 0 && h.players == 2, "party games stake nothing");
    h.force(JUNK); h.roll();
    CHECK(h.score(YACHT) == EV_ZERO, "scratch the yacht");
    CHECK(h.cur == 1 && h.round == 0, "next player");
    h.force(JUNK); h.roll(); h.score(CHANCE);
    CHECK(h.cur == 0 && h.round == 1, "next round");
    h.force(Y2); h.roll();
    CHECK(h.legal(TWOS) && !h.legal(FULL), "joker after a scratch: upper box first");
    CHECK(h.score(TWOS) == 0 && h.card[0].extra == 0, "no bonus on a scratched yacht");
    // Holding, and the three rolls.
    h.force(JUNK); h.roll(); h.score(ONES);
    h.force(JUNK); h.roll();
    h.held = 0x1E;
    uint8_t before[5];
    memcpy(before, h.dice, 5);
    h.roll();
    CHECK(!memcmp(before + 1, h.dice + 1, 4), "held dice stay");
    h.held = 31;
    CHECK(!h.canRoll(), "nothing to roll with every die held");
    h.held = 0;
    h.roll();
    CHECK(h.rollsLeft == 0 && !h.canRoll(), "three rolls a turn");
    // The upper bonus arrives with the box that reaches 63.
    Yacht u;
    memset(&u, 0, sizeof u);
    u.seed(3);
    u.newPurse();
    u.newGame(M_SOLO);
    for (uint8_t f = 1; f <= 6; f++) {
        uint8_t d[5] = {f, f, f, (uint8_t)(f == 6 ? 1 : 6), (uint8_t)(f == 1 ? 2 : 1)};
        u.force(d); u.roll();
        uint8_t ev = u.score((uint8_t)(f - 1));
        CHECK(ev == (f == 6 ? EV_UPPER : 0), "upper bonus with the sixes (f %d ev %d)", f, ev);
    }
    CHECK(u.card[0].upper() == 63 && u.card[0].total() == 98, "63 + 35");
    CHECK(u.purse == 500 - 5 + 1, "upper bonus pays: %d", (int)u.purse);
    CHECK(Yacht::tier(0) == 0 && Yacht::tier(Yacht::TIER_AT[0]) == Yacht::TIER_PAYS[0] &&
          Yacht::tier(1575) == Yacht::TIER_PAYS[Yacht::TIERS - 1], "paytable ends");
}

// One whole game with the house player on every seat.
static void playOut(Yacht &y) {
    while (!y.over) {
        y.roll();
        while (y.rollsLeft) {
            ai::Think t;
            ai::begin(t);
            while (!ai::step(y, t)) {}
            if (t.best == 31) break;
            y.held = t.best;
            y.roll();
        }
        uint8_t cat = ai::bestCat(y.card[y.cur], y.dice);
        CHECK(y.legal(cat), "the house picked an illegal box %d", cat);
        if (!y.legal(cat)) return;
        y.score(cat);
    }
}

static void testHouse() {
    const int GAMES = 20000;
    std::vector<int> scores;
    long long sum = 0, back = 0, yachts = 0, bonus = 0, instant = 0;
    Yacht y;
    memset(&y, 0, sizeof y);
    y.seed(12345);
    for (int n = 0; n < GAMES; n++) {
        y.purse = 1000000;
        y.newGame(M_SOLO);
        int32_t before = y.purse;
        playOut(y);
        int t = y.card[0].total();
        scores.push_back(t);
        sum += t;
        back += y.purse - before;
        instant += y.purse - before - y.endPay;
        yachts += y.card[0].score[YACHT] == 50;
        bonus += y.card[0].bonus();
        CHECK(y.card[0].filled == 0x1FFF && y.round == 13, "a full card");
    }
    std::sort(scores.begin(), scores.end());
    double ret = (double)back / (5.0 * GAMES);
    printf("house, solo: mean %.1f, median %d, 10%% %d, 90%% %d, max %d; yacht %.1f%%, upper bonus %.1f%%\n",
           (double)sum / GAMES, scores[GAMES / 2], scores[GAMES / 10], scores[GAMES * 9 / 10], scores.back(),
           100.0 * yachts / GAMES, 100.0 * bonus / GAMES);
    for (int i = 0; i < Yacht::TIERS; i++) {
        int at = Yacht::TIER_AT[i];
        long n = scores.end() - std::lower_bound(scores.begin(), scores.end(), at);
        printf("  %3d+ pays %2dx: reached %.1f%%\n", at, Yacht::TIER_PAYS[i], 100.0 * n / GAMES);
    }
    if (getenv("YACHT_CURVE")) {            // for setting the paytable
        for (int at = 180; at <= 520; at += 10)
            printf("  P(>=%d) = %.2f%%\n", at,
                   100.0 * (scores.end() - std::lower_bound(scores.begin(), scores.end(), at)) / GAMES);
    }
    printf("  instant bonuses: %.3f antes a game\n", (double)instant / (5.0 * GAMES));
    printf("  return on the ante: %.1f%%\n", 100.0 * ret);
    CHECK(sum / GAMES >= 190, "the house should play a decent game (mean %lld)", sum / GAMES);
    CHECK(ret > 0.85 && ret < 1.0, "return %.3f outside 85..100%%", ret);
    // Versus: the seats are even.
    int first = 0, second = 0;
    for (int n = 0; n < 4000; n++) {
        y.purse = 1000000;
        y.newGame(M_CPU);
        playOut(y);
        uint8_t w = y.winner();
        first += w == 0; second += w == 1;
    }
    printf("house vs house: first seat %d, second %d, ties %d\n", first, second, 4000 - first - second);
}

int main() {
    testBoxes();
    testRules();
    testHouse();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
