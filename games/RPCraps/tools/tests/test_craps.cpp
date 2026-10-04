// Host tests for the craps rules (Craps.cpp).
//
//   rpgame test
//
// 1. Every bet x every point state x 36 rolls against an oracle written
//    separately from the game's own decide().
// 2. Payout rounding, come bets travelling, odds off on the come-out.
// 3. Locks and refusals; odds and lay limits for each ODDS setting; the
//    Beginner table.
// 4. Exact house edges, computed from the rules by enumerating the dice.
// 5. A long fuzz: money is conserved, nothing goes negative, play never stalls.
// 6. The dice: chi-square over 36 cells.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../Craps.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *NAME[BET_COUNT] = {
    "PASS", "DONT", "PASS_ODDS", "DONT_ODDS", "COME", "FIELD",
    "PLACE4", "PLACE5", "PLACE6", "PLACE8", "PLACE9", "PLACE10",
    "HARD4", "HARD6", "HARD8", "HARD10", "ANY7", "ANYCRAPS", "YO",
    "COME4", "COME5", "COME6", "COME8", "COME9", "COME10",
    "CODDS4", "CODDS5", "CODDS6", "CODDS8", "CODDS9", "CODDS10",
};

static Craps fresh(uint8_t table = TABLE_CLASSIC, uint8_t odds = ODDS_345) {
    Craps c;
    memset(&c, 0, sizeof c);
    c.opt.table = table;
    c.opt.odds = odds;
    c.newGame();
    c.purse = 1000000;
    c.seed(12345);
    return c;
}

// A table with a point on n (thrown by settle, as in play).
static Craps withPoint(uint8_t n, uint8_t odds = ODDS_345) {
    Craps c = fresh(TABLE_CLASSIC, odds);
    c.add(PASS, 60);
    c.settle((uint8_t)(n / 2), (uint8_t)(n - n / 2));
    return c;
}

// ---------------------------------------------------------------------------
// 1. The oracle: what a dealer would do, written out bet by bet.
// ---------------------------------------------------------------------------
struct Want { uint8_t kind; int32_t win; uint8_t to; };

static Want oracle(uint8_t b, uint16_t amt, uint8_t point, uint8_t d1, uint8_t d2) {
    int s = d1 + d2;
    bool hard = d1 == d2;
    Want w = {R_NONE, 0, 0};
    if (!amt) return w;
    auto W = [&](int32_t x, bool home) { w.kind = home ? R_WIN_HOME : R_WIN; w.win = x; };
    auto L = [&]() { w.kind = R_LOSE; };
    // true odds a:b as (win per unit) numerator/denominator
    auto pays = [&](int n, int num46, int den46, int num59, int den59, int num68, int den68) {
        if (n == 4 || n == 10) return (int32_t)amt * num46 / den46;
        if (n == 5 || n == 9) return (int32_t)amt * num59 / den59;
        return (int32_t)amt * num68 / den68;
    };
    switch (b) {
        case PASS:
            if (!point) { if (s == 7 || s == 11) W(amt, false); else if (s == 2 || s == 3 || s == 12) L(); }
            else { if (s == point) W(amt, false); else if (s == 7) L(); }
            break;
        case DONT:
            if (!point) { if (s == 2 || s == 3) W(amt, false); else if (s == 7 || s == 11) L(); }
            else { if (s == 7) W(amt, false); else if (s == point) L(); }
            break;
        case PASS_ODDS:
            if (s == point) W(pays(point, 2, 1, 3, 2, 6, 5), true); else if (s == 7) L();
            break;
        case DONT_ODDS:
            if (s == 7) W(pays(point, 1, 2, 2, 3, 5, 6), true); else if (s == point) L();
            break;
        case COME:
            if (s == 7 || s == 11) W(amt, true);
            else if (s == 2 || s == 3 || s == 12) L();
            else { w.kind = R_MOVE; w.to = (uint8_t)(COME4 + box((uint8_t)s)); }
            break;
        case FIELD:
            if (s == 2) W(amt * 2, false);
            else if (s == 12) W(amt * 3, false);
            else if (s == 3 || s == 4 || s == 9 || s == 10 || s == 11) W(amt, false);
            else L();
            break;
        case ANY7: if (s == 7) W(amt * 4, false); else L(); break;
        case ANYCRAPS: if (s == 2 || s == 3 || s == 12) W(amt * 7, false); else L(); break;
        case YO: if (s == 11) W(amt * 15, false); else L(); break;
        default: break;
    }
    if (b >= PLACE4 && b <= PLACE10 && point) {
        int n = BOX_NUM[b - PLACE4];
        if (s == n) W(pays(n, 9, 5, 7, 5, 7, 6), false); else if (s == 7) L();
    }
    if (b >= HARD4 && b <= HARD10 && point) {
        int n = 4 + 2 * (b - HARD4);
        if (s == n && hard) W(amt * (n == 4 || n == 10 ? 7 : 9), false);
        else if (s == 7 || s == n) L();
    }
    if (b >= COME4 && b <= COME10) {
        int n = BOX_NUM[b - COME4];
        if (s == n) W(amt, true); else if (s == 7) L();
    }
    if (b >= CODDS4) {
        int n = BOX_NUM[b - CODDS4];
        if (s == n) { if (point) W(pays(n, 2, 1, 3, 2, 6, 5), true); else w.kind = R_RETURN; }
        else if (s == 7) { if (point) L(); else w.kind = R_RETURN; }
    }
    return w;
}

// Load every spot that can hold chips in this state with $60 (a multiple
// that every payout divides exactly), come points and their odds included.
static void loadAll(Craps &c) {
    for (uint8_t i = 0; i < 6; i++) { c.bet[COME4 + i] = 60; c.bet[CODDS4 + i] = 60; }
    for (uint8_t b = 0; b < COME4; b++) {
        if (c.point && (b == PASS)) continue;             // already on (withPoint)
        if (c.point && b == DONT) { c.bet[DONT] = 60; continue; }
        c.add(b, 60);
    }
}

static void testOracle() {
    for (int pi = -1; pi < 6; pi++) {
        uint8_t point = pi < 0 ? 0 : BOX_NUM[pi];
        for (uint8_t a = 1; a <= 6; a++)
            for (uint8_t b = 1; b <= 6; b++) {
                Craps c = point ? withPoint(point) : fresh();
                loadAll(c);
                Craps before = c;
                c.settle(a, b);
                for (uint8_t k = 0; k < BET_COUNT; k++) {
                    Want w = oracle(k, before.bet[k], point, a, b);
                    const Res &r = c.res[k];
                    CHECK(r.kind == w.kind && r.win == w.win && (w.kind != R_MOVE || r.to == w.to),
                          "point %d roll %d+%d %s ($%d): got kind %d win %d, want kind %d win %d",
                          point, a, b, NAME[k], before.bet[k], r.kind, (int)r.win, w.kind, (int)w.win);
                }
                // Money: the purse moved by exactly what the results say.
                int32_t delta = 0;
                for (uint8_t k = 0; k < BET_COUNT; k++) {
                    const Res &r = c.res[k];
                    if (r.kind == R_WIN) delta += r.win;
                    if (r.kind == R_WIN_HOME) delta += r.win + r.stake;
                    if (r.kind == R_RETURN) delta += r.stake;
                }
                CHECK(c.purse - before.purse == delta, "point %d roll %d+%d: purse moved %d, results say %d",
                      point, a, b, (int)(c.purse - before.purse), (int)delta);
                CHECK(c.purse + c.tableTotal() == before.purse + before.tableTotal() + c.rollWon - c.rollLost,
                      "point %d roll %d+%d: money not conserved", point, a, b);
            }
    }
}

// ---------------------------------------------------------------------------
// 2. Rounding and flows
// ---------------------------------------------------------------------------
static void testRounding() {
    CHECK(Craps::placeWin(6, 5) == 5, "$5 place 6 pays $5");
    CHECK(Craps::placeWin(6, 6) == 7, "$6 place 6 pays $7");
    CHECK(Craps::placeWin(4, 6) == 10, "$6 place 4 pays $10");
    CHECK(Craps::placeWin(5, 5) == 7, "$5 place 5 pays $7");
    CHECK(Craps::oddsWin(5, 5) == 7, "$5 odds on 5 pay $7");
    CHECK(Craps::oddsWin(6, 5) == 6, "$5 odds on 6 pay $6");
    CHECK(Craps::oddsWin(4, 5) == 10, "$5 odds on 4 pay $10");
    CHECK(Craps::layWin(4, 5) == 2, "$5 lay on 4 wins $2");
    CHECK(Craps::layWin(9, 6) == 4, "$6 lay on 9 wins $4");
    CHECK(Craps::layWin(8, 6) == 5, "$6 lay on 8 wins $5");
}

static void testFlows() {
    // Naturals and craps on the come-out; the bets stay up.
    Craps c = fresh();
    c.add(PASS, 10); c.add(DONT, 10);
    c.settle(3, 4);
    CHECK(c.bet[PASS] == 10 && c.bet[DONT] == 0 && c.point == 0, "come-out 7: pass stays, don't lost");
    c.add(DONT, 10);
    c.settle(6, 6);
    CHECK(c.bet[PASS] == 0 && c.bet[DONT] == 10 && c.res[DONT].kind == R_NONE, "12: pass lost, don't barred");
    c.add(PASS, 10);
    c.settle(1, 2);
    CHECK(c.bet[PASS] == 0 && c.res[DONT].kind == R_WIN, "3: pass lost, don't wins");

    // Point, odds, point made.
    c = fresh();
    c.add(PASS, 10);
    c.settle(2, 4);
    CHECK(c.point == 6 && c.histKind[0] == H_POINT, "point 6 set");
    uint16_t got;
    CHECK(c.add(PASS_ODDS, 100, &got) == D_OK && got == 50, "3-4-5X: odds on 6 capped at 5x ($50), got %d", got);
    int32_t p0 = c.purse;
    c.settle(3, 3);
    CHECK(c.point == 0 && c.bet[PASS] == 10 && c.bet[PASS_ODDS] == 0, "point made: flat stays, odds home");
    CHECK(c.purse - p0 == 10 + 50 + 60, "paid 10 + odds 50 back + 60 win, got %d", (int)(c.purse - p0));
    CHECK(c.handPoints == 1 && c.stats.pointsMade == 1, "a point made counts");

    // Seven out ends the hand.
    c.settle(4, 4);
    c.add(PASS_ODDS, 50);
    c.settle(3, 4);
    CHECK(c.point == 0 && c.tableTotal() == 0 && c.histKind[0] == H_SEVEN_OUT, "seven out clears the line");
    CHECK(c.handRolls == 0 && c.handPoints == 0 && c.stats.sevenOuts == 1 && c.stats.longestHand == 4,
          "seven out: new shooter (hand was %d rolls)", c.stats.longestHand);

    // Come bets travel; off and on; odds off on the come-out.
    c = fresh();
    c.add(PASS, 10);
    CHECK(c.add(COME, 5) == D_COME_CLOSED, "come is closed on the come-out");
    c.settle(2, 2);                                                  // point 4
    CHECK(c.add(COME, 5) == D_OK, "come opens with a point");
    c.settle(4, 4);                                                  // 8: come travels
    CHECK(c.bet[COME] == 0 && c.bet[COME8] == 5 && c.res[COME].kind == R_MOVE && c.res[COME].to == COME8,
          "come moves to 8");
    CHECK(c.add(CODDS8, 100, &got) == D_OK && got == 25, "come odds on 8: 5x = $25, got %d", got);
    c.add(COME, 7);
    p0 = c.purse;
    c.settle(5, 3);                                                  // 8 again: off and on
    CHECK(c.res[COME8].kind == R_WIN_HOME && c.res[CODDS8].kind == R_WIN_HOME && c.bet[COME8] == 7 &&
          c.bet[CODDS8] == 0, "off and on: old come 8 home with its odds, the new one moves in");
    CHECK(c.purse - p0 == 5 + 5 + 25 + 30, "off and on pays 5+5 and 25+30, got %d", (int)(c.purse - p0));
    c.add(CODDS8, 35);
    c.settle(1, 3);                                                  // point 4 made -> come-out
    CHECK(c.point == 0 && c.bet[COME8] == 7 && c.bet[CODDS8] == 35, "come 8 rides through the point");
    c.add(PLACE6, 12); c.add(HARD8, 5);
    p0 = c.purse;
    c.settle(4, 4);                                                  // come-out 8: flat wins, odds back
    CHECK(c.res[COME8].kind == R_WIN_HOME && c.res[CODDS8].kind == R_RETURN, "come-out: come odds return unpaid");
    CHECK(c.purse - p0 == 7 + 7 + 35, "come-out come win: 7+7, odds 35 back, got %d", (int)(c.purse - p0));
    CHECK(c.res[HARD8].kind == R_NONE && c.bet[HARD8] == 5, "hardways are off on the come-out");
    CHECK(c.point == 8, "and 8 is the new point");
    c.settle(3, 3);
    CHECK(c.res[PLACE6].kind == R_WIN && c.res[PLACE6].win == 14 && c.bet[PLACE6] == 12, "place 6 pays 14, stays");
    c.settle(1, 6);
    CHECK(c.bet[PLACE6] == 0 && c.bet[HARD8] == 0 && c.res[PLACE6].kind == R_LOSE, "seven out takes the place bets");

    // Come-out 7 with a come point up: flat loses, odds come back.
    c = fresh();
    c.add(PASS, 10); c.settle(2, 3);                                // point 5
    c.add(COME, 10); c.settle(4, 5);                                // come to 9
    c.settle(1, 4);                                                 // point made
    c.add(CODDS9, 40);
    p0 = c.purse;
    c.settle(2, 5);
    CHECK(c.res[COME9].kind == R_LOSE && c.res[CODDS9].kind == R_RETURN && c.purse - p0 == 40 + 10,
          "come-out 7: come point lost, its odds return (purse +%d)", (int)(c.purse - p0));
}

// ---------------------------------------------------------------------------
// 3. Locks, limits and the Beginner table
// ---------------------------------------------------------------------------
static void testLocks() {
    Craps c = fresh();
    CHECK(c.canRoll() == D_ROLL_LINE, "no line bet: no roll");
    CHECK(c.add(PASS_ODDS, 5) == D_NEED_POINT, "odds need a point");
    c.add(PASS, 10);
    CHECK(c.canRoll() == D_OK, "a line bet lets the shooter roll");
    CHECK(c.take(PASS, 5) == 5 && c.bet[PASS] == 5, "pass can come down on the come-out");
    c.settle(5, 5);                                                  // point 10
    CHECK(c.canTake(PASS) == D_CONTRACT && c.take(PASS, 5) == 0, "pass is a contract bet");
    CHECK(c.add(PASS, 5) == D_COMEOUT, "no adding to the pass line after the point");
    CHECK(c.add(DONT, 5) == D_COMEOUT, "no don't pass after the point");
    CHECK(c.add(DONT_ODDS, 5) == D_NEED_LINE, "lay needs a don't bet");
    uint16_t got;
    CHECK(c.add(PASS_ODDS, 100, &got) == D_OK && got == 15, "3x on 10: $15, got %d", got);
    CHECK(c.add(PASS_ODDS, 1) == D_MAX_ODDS, "odds full");
    CHECK(c.add(PLACE6, 1000, &got) == D_OK && got == Craps::TABLE_MAX, "place capped at the table max");
    CHECK(c.add(PLACE6, 1) == D_MAX, "table max");
    CHECK(c.canRoll() == D_OK, "with a point the shooter can always roll");

    // Don't pass may come down after the point; its lay follows.
    c = fresh();
    c.add(DONT, 30);
    c.settle(4, 5);                                                  // point 9
    CHECK(c.add(DONT_ODDS, 1000, &got) == D_OK && got == 180, "3-4-5X lay on 9: 6x = $180, got %d", got);
    CHECK(c.take(DONT, 10) == 10 && c.bet[DONT_ODDS] == 120, "lay shrinks with the don't (%d)", c.bet[DONT_ODDS]);
    int32_t p0 = c.purse;
    CHECK(c.takeDown(DONT) == 20 && c.bet[DONT_ODDS] == 0 && c.purse - p0 == 140, "don't down: lay comes home");

    // Odds limits per setting.
    struct { uint8_t odds, n; uint16_t odd, lay; } L[] = {
        {ODDS_345, 4, 30, 60}, {ODDS_345, 5, 40, 60}, {ODDS_345, 6, 50, 60},
        {ODDS_2X, 4, 20, 40}, {ODDS_2X, 9, 20, 30}, {ODDS_2X, 8, 20, 24},
        {ODDS_10X, 10, 100, 200}, {ODDS_10X, 5, 100, 150}, {ODDS_10X, 6, 100, 120},
    };
    for (auto &l : L) {
        Craps d = fresh(TABLE_CLASSIC, l.odds);
        d.add(PASS, 10); d.add(DONT, 10);
        d.settle((uint8_t)(l.n / 2), (uint8_t)(l.n - l.n / 2));
        CHECK(d.limit(PASS_ODDS) == l.odd && d.limit(DONT_ODDS) == l.lay,
              "odds option %d point %d: odds %d lay %d, want %d %d", l.odds, l.n, d.limit(PASS_ODDS),
              d.limit(DONT_ODDS), l.odd, l.lay);
        CHECK(Craps::layWin(l.n, d.limit(DONT_ODDS)) <= 10 * d.oddsMultiple(l.n), "a full lay wins <= k x flat");
    }

    // Come points can't come down; empty spots refuse.
    c = fresh();
    c.add(PASS, 5); c.settle(3, 3); c.add(COME, 5); c.settle(2, 2);
    CHECK(c.canTake(COME4) == D_CONTRACT, "come points stay up");
    CHECK(c.add(COME4, 5) == D_CLOSED, "only the dealer puts chips on a come point");
    CHECK(c.canTake(FIELD) == D_EMPTY, "nothing to take");

    // Out of chips: a $25 chip with $7 left puts $7 down.
    c = fresh();
    c.purse = 7;
    CHECK(c.add(FIELD, 25, &got) == D_OK && got == 7 && c.purse == 0, "the last $7");
    CHECK(c.add(PASS, 1) == D_NO_CHIPS, "purse empty");

    // Beginner: only the line, odds, field and place 6/8.
    c = fresh(TABLE_BEGINNER);
    CHECK(c.add(PLACE6, 6) == D_OK && c.add(PLACE5, 5) == D_CLOSED && c.add(HARD8, 5) == D_CLOSED &&
          c.add(ANY7, 5) == D_CLOSED && c.add(FIELD, 5) == D_OK, "beginner spots");
    c.add(PASS, 5); c.settle(2, 2);
    CHECK(c.add(COME, 5) == D_CLOSED, "no come on the beginner table");
    CHECK(!c.classicBetsUp(), "beginner bets are not classic-only");
    c = fresh();
    c.add(YO, 1);
    CHECK(c.classicBetsUp(), "a yo bet is classic-only");
}

// ---------------------------------------------------------------------------
// 4. House edge, exactly: enumerate the dice. A bet that stays up through
//    rolls that don't decide it is valued per decision.
// ---------------------------------------------------------------------------
// Value (expected net, per $1) of spot b on table c, conditioned on rolls
// that decide it; a bet that becomes another (come -> come point, pass ->
// point) is followed one more step.
static double value(const Craps &c, uint8_t b, int depth = 0) {
    double sum = 0, n = 0;
    for (uint8_t a = 1; a <= 6; a++)
        for (uint8_t d = 1; d <= 6; d++) {
            Craps t = c;
            t.settle(a, d);
            const Res &r = t.res[b];
            double stake = r.stake;
            switch (r.kind) {
                case R_WIN: case R_WIN_HOME: sum += r.win / stake; n++; break;
                case R_LOSE: sum -= 1; n++; break;
                case R_RETURN: n++; break;
                case R_MOVE: sum += value(t, r.to, depth + 1); n++; break;
                case R_NONE:
                    // A come-out that doesn't decide the line: either a point
                    // to make now, or the Don't's barred 12 (a push).
                    if (depth == 0 && !c.point && (b == PASS || b == DONT)) {
                        if (t.point) sum += value(t, b, 1);
                        n++;
                    }
                    break;
            }
        }
    return n ? sum / n : 0;
}

static void testEdges() {
    struct { const char *name; uint8_t b; int point; double want; } E[] = {
        {"pass", PASS, 0, -7.0 / 495}, {"don't pass", DONT, 0, -3.0 / 220},
        {"field", FIELD, 0, -1.0 / 36}, {"any 7", ANY7, 0, -1.0 / 6},
        {"any craps", ANYCRAPS, 0, -1.0 / 9}, {"yo", YO, 0, -1.0 / 9},
        {"place 6", PLACE6, 4, -1.0 / 66}, {"place 8", PLACE8, 4, -1.0 / 66},
        {"place 5", PLACE5, 4, -1.0 / 25}, {"place 9", PLACE9, 4, -1.0 / 25},
        {"place 4", PLACE4, 6, -1.0 / 15}, {"place 10", PLACE10, 6, -1.0 / 15},
        {"hard 6", HARD6, 4, -1.0 / 11}, {"hard 8", HARD8, 4, -1.0 / 11},
        {"hard 4", HARD4, 6, -1.0 / 9}, {"hard 10", HARD10, 6, -1.0 / 9},
        {"come", COME, 4, -7.0 / 495},
        {"odds on 4", PASS_ODDS, 4, 0}, {"odds on 5", PASS_ODDS, 5, 0}, {"odds on 6", PASS_ODDS, 6, 0},
        {"lay on 4", DONT_ODDS, 4, 0}, {"lay on 5", DONT_ODDS, 5, 0}, {"lay on 6", DONT_ODDS, 6, 0},
    };
    for (auto &e : E) {
        Craps c = e.point ? withPoint((uint8_t)e.point) : fresh();
        if (e.b == DONT_ODDS) { c = fresh(); c.add(DONT, 60); c.settle((uint8_t)(e.point / 2), (uint8_t)(e.point - e.point / 2)); }
        if (e.b != PASS) c.bet[PASS] = 0;                // only the bet being valued
        c.bet[e.b] = 0;
        if (e.b == DONT_ODDS) { c.bet[DONT] = 60; c.bet[DONT_ODDS] = 60; }
        else if (e.b == PASS_ODDS) { c.bet[PASS] = 60; c.bet[PASS_ODDS] = 60; }
        else c.bet[e.b] = 60;
        double v = value(c, e.b);
        CHECK(fabs(v - e.want) < 1e-9, "edge %s: %.5f%%, want %.5f%%", e.name, v * 100, e.want * 100);
    }
}

// ---------------------------------------------------------------------------
// 5. Fuzz
// ---------------------------------------------------------------------------
static uint32_t frng = 99;
static uint32_t fr() { frng ^= frng << 13; frng ^= frng >> 17; frng ^= frng << 5; return frng; }

static void testFuzz() {
    static const uint16_t CHIP[4] = {1, 5, 25, 100};
    for (int game = 0; game < 40; game++) {
        Craps c;
        memset(&c, 0, sizeof c);
        c.opt.table = (uint8_t)(fr() % 2);
        c.opt.odds = (uint8_t)(fr() % 3);
        c.seed(fr());
        c.newGame();
        int64_t money = c.purse, house = 0;
        for (int step = 0; step < 5000; step++) {
            uint32_t r = fr() % 10;
            if (r < 5) {
                uint8_t b = (uint8_t)(fr() % BET_COUNT);
                uint16_t got = 0;
                c.add(b, CHIP[fr() % 4], &got);
            } else if (r < 7) {
                uint8_t b = (uint8_t)(fr() % BET_COUNT);
                if (fr() & 1) c.take(b, CHIP[fr() % 4]); else c.takeDown(b);
            } else {
                if (c.canRoll() != D_OK) {
                    // Can always make progress: bet the line, or take something down.
                    bool progress = c.purse > 0 || c.broke();
                    for (uint8_t b = 0; b < BET_COUNT && !progress; b++) progress = c.canTake(b) == D_OK;
                    CHECK(progress, "game %d step %d: stalled (purse %d, table %d)", game, step,
                          (int)c.purse, (int)c.tableTotal());
                    if (c.purse > 0) c.add(fr() & 1 ? PASS : DONT, CHIP[fr() % 4]);
                    else for (uint8_t b = 0; b < BET_COUNT; b++) c.takeDown(b);
                    continue;
                }
                c.throwDice();
                house += c.rollLost - c.rollWon;
            }
            CHECK(c.purse >= 0, "game %d: purse went negative", game);
            CHECK(c.purse + c.tableTotal() + house == money, "game %d step %d: money not conserved", game, step);
            if (!c.point) CHECK(!c.bet[PASS_ODDS] && !c.bet[DONT_ODDS] && !c.bet[COME], "game %d: odds/come without a point", game);
            for (uint8_t b = 0; b < BET_COUNT; b++)
                if (b == PASS_ODDS || b == DONT_ODDS || b >= CODDS4)
                    CHECK(c.bet[b] <= c.limit(b), "game %d: %s over its limit", game, NAME[b]);
            for (uint8_t i = 0; i < 6; i++)
                CHECK(!c.bet[CODDS4 + i] || c.bet[COME4 + i], "game %d: come odds without a come point", game);
            if (c.broke()) { c.newGame(); money = c.purse; house = 0; }
            if (fails > 20) return;
        }
    }
}

// ---------------------------------------------------------------------------
// 6. Dice
// ---------------------------------------------------------------------------
static void testDice() {
    Craps c = fresh();
    c.add(FIELD, 1);
    uint32_t cell[36] = {0};
    const uint32_t N = 360000;
    for (uint32_t i = 0; i < N; i++) {
        c.purse = 1000; c.bet[FIELD] = 0;
        c.throwDice();
        cell[(c.d1 - 1) * 6 + (c.d2 - 1)]++;
    }
    double e = N / 36.0, chi = 0;
    for (int i = 0; i < 36; i++) chi += (cell[i] - e) * (cell[i] - e) / e;
    CHECK(chi < 66.6, "dice chi-square %.1f over 35 degrees of freedom (99.9%%: 66.6)", chi);
    printf("dice: chi-square %.1f (35 dof)\n", chi);

    c.force(6, 6);
    c.throwDice();
    CHECK(c.d1 == 6 && c.d2 == 6, "forced roll");
}

int main() {
    testOracle();
    testRounding();
    testFlows();
    testLocks();
    testEdges();
    testFuzz();
    testDice();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
