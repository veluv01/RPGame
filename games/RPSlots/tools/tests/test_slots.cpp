// Host tests for Slots.cpp (rpgame test).
//
//   * LUCKY 7's return, exactly: every one of the 35^3 stops.
//   * DRAGON FORTUNE's line evaluation against a slow reference written a
//     different way (rows spelled out, the wild applied per cell).
//   * Its return by Monte Carlo, whole games at a time (free games and Hold
//     and Spin included), split by where the money came from.
//   * Money is conserved: the purse only ever moves by the bet and by what
//     the result says was won.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../Slots.h"

static long checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; if (fails < 20) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long x_ = (a), y_ = (b); if (x_ != y_) { fails++; \
    if (fails < 20) printf("FAIL %s:%d %s = %lld, want %lld\n", __FILE__, __LINE__, #a, x_, y_); } } while (0)

// The lines again, as rows per reel: typed separately from Slots.cpp's
// packed table so a slip in either shows.
static const uint8_t REF_LINE[25][5] = {
    {1,1,1,1,1}, {0,0,0,0,0}, {2,2,2,2,2}, {0,1,2,1,0}, {2,1,0,1,2},
    {0,0,1,0,0}, {2,2,1,2,2}, {1,0,0,0,1}, {1,2,2,2,1}, {1,0,1,0,1},
    {1,2,1,2,1}, {0,1,0,1,0}, {2,1,2,1,2}, {1,1,0,1,1}, {1,1,2,1,1},
    {0,1,1,1,0}, {2,1,1,1,2}, {0,0,1,2,2}, {2,2,1,0,0}, {0,2,0,2,0},
    {2,0,2,0,2}, {1,0,2,0,1}, {1,2,0,2,1}, {0,2,2,2,0}, {2,0,0,0,2},
};

static Slots g;

static void fresh(uint8_t machine, uint8_t betIdx, uint32_t seed) {
    memset(&g, 0, sizeof g);
    g.newGame();
    g.machine = machine;
    g.betIdx[machine] = betIdx;
    g.seed(seed);
}

static void testClassicExact() {
    fresh(M_CLASSIC, 0, 1);
    uint64_t won = 0, hits = 0, n = 0;
    uint64_t wheels = 0, wheelSum = 0;
    int seen[WHEEL_SEGS] = {0};
    for (uint8_t s = 0; s < WHEEL_SEGS; s++) wheelSum += Slots::wheelPrize(s);
    uint8_t len = g.stripLen();
    CHECK_EQ(len, 35);
    for (uint8_t a = 0; a < len; a++)
        for (uint8_t b = 0; b < len; b++)
            for (uint8_t c = 0; c < len; c++) {
                uint8_t stops[5] = {a, b, c, 0, 0};
                g.purse = 100;
                g.force(stops);
                g.spin();
                CHECK_EQ(g.res.stop[1], b);
                CHECK_EQ(g.purse, 100 - 1 + g.res.total);
                uint8_t x = g.stripSym(0, a + 1), y = g.stripSym(1, b + 1), z = g.stripSym(2, c + 1);
                CHECK_EQ(g.res.grid[2][1], z);
                CHECK_EQ(g.res.linePay, Slots::classicPay(x, y, z));
                CHECK_EQ(g.res.linePay, g.linePay(0));
                CHECK((g.res.linePay > 0) == (g.res.nLines == 1));
                // Two lucky charms on the line spin the wheel; its prize is one of twelve.
                int lucky = (x == C_CLOVER || x == C_HORSESHOE) + (y == C_CLOVER || y == C_HORSESHOE) +
                            (z == C_CLOVER || z == C_HORSESHOE);
                CHECK_EQ(g.res.wheel != 0, lucky >= 2);
                CHECK_EQ(g.res.wheelPay, g.res.wheel ? Slots::wheelPrize((uint8_t)(g.res.wheel - 1)) : 0);
                CHECK_EQ(g.res.total, g.res.linePay + g.res.wheelPay);
                if (g.res.wheel) { wheels++; seen[g.res.wheel - 1]++; }
                won += (uint64_t)g.res.linePay * 12 + (g.res.wheel ? wheelSum : 0);     // in twelfths
                hits += g.res.total > 0; n++;
            }
    double rtp = (double)won / 12.0 / (double)n, hit = (double)hits / (double)n;
    printf("classic: return %.2f%%, a win on %.1f%% of pulls, the wheel 1 in %.0f (%llu stops)\n", rtp * 100,
           hit * 100, (double)n / (double)wheels, (unsigned long long)n);
    for (uint8_t s = 0; s < WHEEL_SEGS; s++) CHECK(seen[s] > 0);       // every segment comes up
    CHECK(rtp > 0.94 && rtp < 0.97);
    CHECK(hit > 0.25 && hit < 0.35);
    // The pays scale with the bet.
    for (uint8_t bi = 0; bi < 4; bi++) {
        fresh(M_CLASSIC, bi, 5);
        uint8_t stops[5] = {0, 0, 0, 0, 0};
        // Find SEVEN on each reel's middle row.
        for (uint8_t r = 0; r < 3; r++)
            for (uint8_t s = 0; s < len; s++) if (g.stripSym(r, s + 1) == C_SEVEN) { stops[r] = s; break; }
        g.force(stops);
        int32_t before = g.purse;
        g.spin();
        CHECK_EQ(g.purse - before, 250 * g.bet() - g.bet());
        CHECK_EQ(g.res.lineCount[0], 3);
        CHECK_EQ(g.res.lineSym[0], C_SEVEN);
    }
}

// Line wins of the result in g.res, the slow way.
static int32_t refLines(uint8_t counts[25]) {
    const Result &r = g.res;
    int32_t pay = 0;
    for (int l = 0; l < 25; l++) {
        counts[l] = 0;
        uint8_t first = r.grid[0][REF_LINE[l][0]];
        if (first == F_DRAGON || first == F_GONG || first == F_COIN) continue;
        int cnt = 0;
        for (int reel = 0; reel < 5; reel++) {
            bool dragonOnReel = false;
            for (int row = 0; row < 3; row++) if (r.grid[reel][row] == F_DRAGON) dragonOnReel = true;
            if (r.grid[reel][REF_LINE[l][reel]] == first || dragonOnReel) cnt++;
            else break;
        }
        if (cnt >= 3) { counts[l] = (uint8_t)cnt; pay += Slots::fortunePay(first, (uint8_t)cnt) * (g.bet() / 5) * (r.wasFree ? 2 : 1); }
    }
    return pay;
}

struct Tally { double bet, lines, scatter, freeLines, hold; uint64_t spins, frees, holds, hits, grands, majors; };

// Whole games: a paid spin and everything it sets off.
static void playFortune(uint64_t games, Tally &t, bool verify) {
    for (uint64_t i = 0; i < games; i++) {
        g.purse = 1000000;
        g.pot[0] = g.pot[1] = 0;
        int32_t before = g.purse;
        t.bet += g.bet();
        t.spins++;
        bool first = true;
        int32_t sum = 0;
        do {
            int32_t p0 = g.purse;
            g.spin();
            const Result &r = g.res;
            if (verify) {
                CHECK_EQ(g.purse - p0, r.total - (first ? g.bet() : 0));
                CHECK_EQ(r.total, r.linePay + r.scatterPay);
                CHECK_EQ(r.wasFree, !first);
                uint8_t counts[25];
                CHECK_EQ(refLines(counts), r.linePay);
                int32_t each = 0;
                uint8_t n = 0;
                for (uint8_t l = 0; l < 25; l++) {
                    CHECK_EQ(counts[l], r.lineCount[l]);
                    CHECK_EQ(Slots::lineRow(l, 2), REF_LINE[l][2]);
                    each += g.linePay(l);
                    n += r.lineCount[l] != 0;
                }
                CHECK_EQ(each, r.linePay);
                CHECK_EQ(n, r.nLines);
                uint8_t gongs = 0, coins = 0;
                for (int c = 0; c < 15; c++) {
                    gongs += r.grid[c / 3][c % 3] == F_GONG;
                    coins += r.grid[c / 3][c % 3] == F_COIN;
                    CHECK((r.grid[c / 3][c % 3] == F_COIN) == (r.coin[c / 3][c % 3] != K_NONE));
                    CHECK(r.coin[c / 3][c % 3] < K_MAJOR);         // MAJOR only lands during the feature
                }
                CHECK_EQ(gongs, r.scatters);
                CHECK_EQ(coins, r.coins);
                CHECK_EQ(r.freeTrigger, gongs >= 3);
                CHECK_EQ(r.holdTrigger, coins >= 6);
                CHECK_EQ(g.holding, r.holdTrigger);
                CHECK(g.freeLeft <= FREE_MAX);
            }
            sum += r.total;
            if (first) t.lines += r.linePay; else t.freeLines += r.linePay;
            t.scatter += r.scatterPay;
            if (r.freeTrigger && first) t.frees++;
            if (r.holdTrigger) {
                t.holds++;
                uint8_t guard = 0;
                int32_t p1 = g.purse;
                uint8_t had = 0;
                for (int c = 0; c < 15; c++) had += g.held[c] != 0;
                CHECK_EQ(had, r.coins);
                while (g.respin()) {
                    CHECK(++guard < 100);
                    CHECK(g.respins >= 1 && g.respins <= HOLD_RESPINS);
                    CHECK_EQ(g.purse, p1);                         // nothing is paid until it ends
                }
                CHECK(!g.holding);
                int32_t want = 0;
                uint8_t n = 0;
                for (int c = 0; c < 15; c++) { want += g.coinValue(g.held[c]); n += g.held[c] != 0; }
                if (n == 15) { t.grands++; CHECK_EQ(g.holdJackpot, J_GRAND + 1); }
                else if (g.holdJackpot == J_MAJOR + 1) t.majors++;
                else CHECK_EQ(g.holdPay, want);                    // (a GRAND or MAJOR win has reset its pot by now)
                CHECK_EQ(g.purse - p1, g.holdPay);
                CHECK(g.holdPay >= 6 * g.bet());
                t.hold += g.holdPay;
                sum += g.holdPay;
            }
            first = false;
        } while (g.freeLeft);
        CHECK_EQ(g.purse - before, sum - g.bet());
        CHECK_EQ(g.lastWin, sum);
        CHECK(!g.inFeature());
        t.hits += sum > 0;
    }
}

static void testFortune() {
    Tally t;
    memset(&t, 0, sizeof t);
    fresh(M_FORTUNE, 0, 12345);
    playFortune(300000, t, true);
    for (uint8_t bi = 1; bi < 4; bi++) {            // the same at every bet
        Tally u;
        memset(&u, 0, sizeof u);
        fresh(M_FORTUNE, bi, 99 + bi);
        playFortune(20000, u, true);
    }
    fresh(M_FORTUNE, 0, 777);
    playFortune(6000000, t, false);
    double b = t.bet;
    double rtp = (t.lines + t.scatter + t.freeLines + t.hold) / b;
    printf("fortune: return %.2f%% over %llu games (pots at zero)\n", rtp * 100, (unsigned long long)t.spins);
    printf("  lines %.2f%%  scatter %.2f%%  free-game lines %.2f%%  hold and spin %.2f%%\n",
           t.lines / b * 100, t.scatter / b * 100, t.freeLines / b * 100, t.hold / b * 100);
    printf("  a win on %.1f%% of games; free games 1 in %.0f; hold and spin 1 in %.0f; MAJOR %llu, GRAND %llu\n",
           (double)t.hits / (double)t.spins * 100, (double)t.spins / (double)t.frees, (double)t.spins / (double)t.holds,
           (unsigned long long)t.majors, (unsigned long long)t.grands);
    CHECK(rtp > 0.93 && rtp < 0.97);
    CHECK(t.frees && t.spins / t.frees > 40 && t.spins / t.frees < 250);
    CHECK(t.holds && t.spins / t.holds > 40 && t.spins / t.holds < 250);
}

// SWEET: every stop, against a reference that spells the five lines out, then
// its return by play (the sugar rush makes each spin depend on the last).
static void testSweet() {
    static const uint8_t ROWS5[5][3] = {{1, 1, 1}, {0, 0, 0}, {2, 2, 2}, {0, 1, 2}, {2, 1, 0}};
    fresh(M_SWEET, 0, 4242);
    uint8_t len = g.stripLen();
    double bet = 0, won = 0;
    uint64_t hits = 0, n = 0, atTop = 0;
    uint8_t rush = 0;
    for (uint64_t i = 0; i < 4000000; i++) {
        bool forced = i < (uint64_t)len * len * len;
        if (forced) {                                   // first, every stop once
            uint8_t stops[5] = {(uint8_t)(i % len), (uint8_t)(i / len % len), (uint8_t)(i / len / len), 0, 0};
            g.force(stops);
        }
        g.purse = 1000;
        g.spin();
        const Result &r = g.res;
        CHECK_EQ(r.mult, Slots::rushMult(rush));
        int32_t want = 0;
        uint8_t lines = 0;
        for (int l = 0; l < 5; l++) {
            uint8_t s[3], first = S_LOLLY;
            for (int k = 0; k < 3; k++) { s[k] = r.grid[k][ROWS5[l][k]]; if (first == S_LOLLY && s[k] != S_LOLLY) first = s[k]; }
            bool hit = true;
            for (int k = 0; k < 3; k++) if (s[k] != S_LOLLY && s[k] != first) hit = false;
            CHECK_EQ(r.lineCount[l] != 0, hit);
            if (hit) { want += Slots::sweetPay(first) * (g.bet() / 5) * r.mult; lines++; CHECK_EQ(g.linePay((uint8_t)l), Slots::sweetPay(first) * (g.bet() / 5) * r.mult); }
        }
        CHECK_EQ(r.total, want);
        CHECK_EQ(r.nLines, lines);
        CHECK_EQ(g.purse, 1000 - g.bet() + want);
        rush = want ? (uint8_t)(rush < 3 ? rush + 1 : 3) : 0;
        CHECK_EQ(g.rush, rush);
        if (!forced) { bet += g.bet(); won += want; hits += want > 0; atTop += r.mult == 5; n++; }
    }
    double rtp = won / bet;
    printf("sweet: return %.2f%% over %llu games, a win on %.1f%%, the x5 on %.2f%% of spins\n", rtp * 100,
           (unsigned long long)n, (double)hits / (double)n * 100, (double)atTop / (double)n * 100);
    CHECK(rtp > 0.93 && rtp < 0.97);
}

static void testBetsAndEnds() {
    fresh(M_CLASSIC, 0, 3);
    g.purse = 7;
    CHECK(g.changeBet(1));              // $5
    CHECK(!g.changeBet(1));             // $10 is beyond the purse
    CHECK_EQ(g.bet(), 5);
    g.purse = 3;
    g.fitBet();
    CHECK_EQ(g.bet(), 1);
    CHECK(!g.changeBet(-1));
    g.purse = 0;
    CHECK(g.broke());
    CHECK(!g.canSpin());
    g.opt.goal = GOAL_1000;
    g.purse = 1000;
    CHECK(g.reachedGoal());
    g.freeLeft = 2;
    CHECK(!g.reachedGoal());            // a feature plays out first
    CHECK(g.canSpin());
    g.freeLeft = 0;
    g.opt.goal = GOAL_ENDLESS;
    CHECK(!g.reachedGoal());
    // The meters: fixed ones scale with the bet, the others also grow.
    fresh(M_FORTUNE, 0, 3);
    CHECK_EQ(g.meter(J_MINI), 100);
    CHECK_EQ(g.meter(J_MINOR), 250);
    CHECK_EQ(g.meter(J_MAJOR), 1000);
    CHECK_EQ(g.meter(J_GRAND), 5000);
    g.purse = 100000;
    for (int paid = 0; paid < 10;) {            // ten paid spins: free games do not feed the pots
        g.spin();
        paid += !g.res.wasFree;
        while (g.holding) g.respin();
    }
    CHECK(g.meter(J_GRAND) >= 5010);
    CHECK(g.meter(J_MAJOR) >= 1005);
    // Forced features land as asked.
    fresh(M_FORTUNE, 0, 3);
    g.forceFeature(1); g.spin();
    CHECK(g.res.freeTrigger); CHECK_EQ(g.freeLeft, FREE_GAMES);
    fresh(M_FORTUNE, 0, 3);
    g.forceFeature(2); g.spin();
    CHECK(g.res.holdTrigger); CHECK(g.holding); CHECK_EQ(g.respins, HOLD_RESPINS);
    fresh(M_FORTUNE, 0, 3);
    g.forceFeature(4); g.spin();
    CHECK(g.res.lineCount[0] == 5 && g.res.lineSym[0] == F_CAT);
    CHECK(g.res.linePay >= 80);
}

int main() {
    testClassicExact();
    testFortune();
    testSweet();
    testBetsAndEnds();
    printf("%ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
