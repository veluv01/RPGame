// Host tests for the rules (Bingo.*): rpgame test
//   test_bingo            run the checks
//   test_bingo --dump     round set-ups for tools/tests/ref_bingo.py
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../../Bingo.h"

static long checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; if (failures < 30) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { failures++; if (failures < 30) printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

enum : uint8_t { A = 1, B = 2, UP = 4, DOWN = 8, LEFT = 16, RIGHT = 32 };

static int popcount(uint32_t m) { int n = 0; for (; m; m &= m - 1) n++; return n; }

static std::vector<Event> tick(Bingo &g, uint8_t pressed = 0, bool busy = false) {
    g.update(pressed, pressed, busy);
    std::vector<Event> out;
    Event e;
    while (g.popEvent(e)) out.push_back(e);
    return out;
}

static bool has(const std::vector<Event> &v, Ev t, Event *out = nullptr) {
    for (auto &e : v) if (e.type == t) { if (out) *out = e; return true; }
    return false;
}

static void fresh(Bingo &g, uint32_t seed) {
    memset((void *)&g, 0, sizeof g);
    g.seed(seed);
    g.newGame();
    tick(g);                                   // Welcome -> Buy
}

// The call on which a card first has a line, the slow way.
static int firstLine(const Bingo &g, const uint8_t *card) {
    uint32_t m = 1u << 12;
    for (int i = 0; i < 75; i++) {
        for (int c = 0; c < 25; c++) if (card[c] == g.balls[i]) m |= 1u << c;
        for (int l = 0; l < NLINES; l++) if ((m & LINES[l]) == LINES[l]) return i + 1;
    }
    return 76;
}

static void testLines() {
    uint32_t all = 0;
    int through12 = 0;
    for (int l = 0; l < NLINES; l++) {
        CHECK_EQ(popcount(LINES[l]), 5);
        CHECK((LINES[l] >> 25) == 0);
        all |= LINES[l];
        if (LINES[l] & (1u << 12)) through12++;
        for (int k = 0; k < l; k++) CHECK(popcount(LINES[l] & LINES[k]) <= 1);
    }
    CHECK_EQ(all, 0x1FFFFFFu);
    CHECK_EQ(through12, 4);
    for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) {
        CHECK(LINES[r] & (1u << (r * 5 + c)));
        CHECK(LINES[5 + c] & (1u << (r * 5 + c)));
    }
    for (int i = 0; i < 5; i++) { CHECK(LINES[10] & (1u << (i * 6))); CHECK(LINES[11] & (1u << (4 + i * 4))); }
    char buf[8];
    *callName(buf, 1) = 0; CHECK(!strcmp(buf, "B-1"));
    *callName(buf, 15) = 0; CHECK(!strcmp(buf, "B-15"));
    *callName(buf, 16) = 0; CHECK(!strcmp(buf, "I-16"));
    *callName(buf, 45) = 0; CHECK(!strcmp(buf, "N-45"));
    *callName(buf, 60) = 0; CHECK(!strcmp(buf, "G-60"));
    *callName(buf, 75) = 0; CHECK(!strcmp(buf, "O-75"));
}

static void testSetup() {
    for (uint32_t seed = 1; seed <= 400; seed++) {
        Bingo g;
        fresh(g, seed * 7919u);
        g.opt.hall = (uint8_t)(seed % 3);
        g.start(9);
        CHECK(g.phase == Phase::Calling);
        CHECK_EQ(g.nCards, 9);
        bool seen[76] = {false};
        for (int i = 0; i < 75; i++) { CHECK(g.balls[i] >= 1 && g.balls[i] <= 75); seen[g.balls[i]] = true; }
        for (int n = 1; n <= 75; n++) CHECK(seen[n]);
        for (int k = 0; k < 9; k++) {
            CHECK_EQ(g.cards[k][12], 0);
            CHECK_EQ(g.daub[k], 1u << 12);
            CHECK_EQ(g.pend[k], 0u);
            for (int i = 0; i < 25; i++) {
                if (i == 12) continue;
                int v = g.cards[k][i], c = i % 5;
                CHECK(v >= c * 15 + 1 && v <= c * 15 + 15);
                for (int j = 0; j < i; j++) CHECK(g.cards[k][j] != v);
            }
        }
        CHECK(g.hallBall >= 4 && g.hallBall <= 75);
        CHECK_EQ(g.pot, 5 * (9 + RIVALS[g.opt.hall]) * 9 / 10);
        CHECK_EQ(g.purse, START_PURSE - 45);
        CHECK_EQ(g.jackpot, JACKPOT_SEED + 2);
    }
}

// A player who daubs everything at once wins exactly when one of the cards
// has its line no later than the hall's; one who never daubs always loses,
// on the call after the hall's.
static void testRace() {
    long wins = 0, rounds = 0, rare = 0;
    long long paidIn = 0, paidOut = 0;
    for (uint32_t seed = 1; seed <= 3000; seed++) {
        Bingo g;
        fresh(g, seed * 2654435761u);
        g.opt.speed = 1;
        g.purse = 100000;
        int32_t before = g.purse, jp = g.jackpot;
        g.start(9);
        int mine = 76;
        for (int k = 0; k < 9; k++) { int f = firstLine(g, g.cards[k]); if (f < mine) mine = f; }
        int32_t pot = g.pot;
        bool lazy = (seed % 5) == 0;
        Event won{}, lost{};
        bool w = false, l = false;
        for (int t = 0; t < 80 * 80 && g.phase == Phase::Calling; t++) {
            auto ev = tick(g);
            if (has(ev, Ev::Call) && !lazy) {
                g.daubAll();
                Event e;
                while (g.popEvent(e)) ev.push_back(e);
            }
            if (has(ev, Ev::Bingo, &won)) w = true;
            if (has(ev, Ev::Rival, &lost)) l = true;
        }
        CHECK(w != l);
        rounds++;
        paidIn += 45;
        if (lazy) {
            CHECK(l);
            CHECK_EQ(g.nCalled, g.hallBall);
            CHECK_EQ(lost.b, mine <= g.hallBall);
            CHECK_EQ(g.purse, before - 45);
            CHECK_EQ(g.jackpot, jp + 2);
            continue;
        }
        CHECK_EQ(w, mine <= g.hallBall);
        if (w) {
            wins++;
            CHECK_EQ(g.nCalled, mine);
            CHECK_EQ(firstLine(g, g.cards[won.a]), mine);
            CHECK((g.daub[won.a] & LINES[won.b]) == LINES[won.b]);
            bool j = mine <= JACKPOT_CALLS;
            CHECK_EQ((won.c & WIN_JACKPOT) != 0, j);
            CHECK_EQ(won.amount, pot + (j ? jp + 2 : 0));
            CHECK_EQ(g.purse, before - 45 + won.amount);
            CHECK_EQ(g.jackpot, j ? JACKPOT_SEED : jp + 2);
            rare += (won.c & WIN_RARE) != 0;
            paidOut += pot;
        } else {
            CHECK_EQ(g.nCalled, g.hallBall);
            CHECK_EQ(g.purse, before - 45);
        }
        // The round ends: back to the buy-in once the show is over.
        CHECK(g.phase == Phase::Won || g.phase == Phase::Lost);
        for (int t = 0; t < 10; t++) tick(g, 0, true);
        CHECK(g.phase == Phase::Won || g.phase == Phase::Lost);
        for (int t = 0; t < 60; t++) tick(g);
        CHECK(g.phase == Phase::Buy);
    }
    printf("perfect play, 9 cards vs 20: won %ld of %ld rounds (%.1f%%), return %.1f%%, "
           "\"IT'S A BINGO!\" %ld of %ld wins\n",
           wins, rounds * 4 / 5, 100.0 * wins / (rounds * 4 / 5), 100.0 * paidOut / (paidIn * 4 / 5), rare, wins);
    CHECK(wins > rounds * 4 / 5 * 25 / 100 && wins < rounds * 4 / 5 * 45 / 100);
    CHECK(rare > wins * 5 / 100 && rare < wins * 16 / 100);
}

// How often the jackpot would fall, from the set-up alone.
static void testJackpotOdds() {
    for (int n : {1, 9}) {
        long hit = 0, N = 60000;
        for (long s = 1; s <= N; s++) {
            Bingo g;
            fresh(g, (uint32_t)s * 40503u + 17);
            g.purse = 1000;
            g.start((uint8_t)n);
            int mine = 76;
            for (int k = 0; k < n; k++) { int f = firstLine(g, g.cards[k]); if (f < mine) mine = f; }
            if (mine <= JACKPOT_CALLS && mine <= g.hallBall) hit++;
        }
        printf("jackpot (a bingo within %d calls), %d card%s: 1 round in %.0f\n", JACKPOT_CALLS, n, n > 1 ? "s" : "",
               hit ? (double)N / hit : 0.0);
        if (n == 9) CHECK(hit > N / 400 && hit < N / 60);
    }
}

static void testButtons() {
    Bingo g;
    fresh(g, 99);
    CHECK(g.phase == Phase::Buy);
    CHECK_EQ(g.buyN, 3);
    tick(g, RIGHT); CHECK_EQ(g.buyN, 4);
    tick(g, LEFT); tick(g, LEFT); tick(g, LEFT); tick(g, LEFT);
    CHECK_EQ(g.buyN, 1);
    for (int i = 0; i < 12; i++) tick(g, UP);
    CHECK_EQ(g.buyN, 9);
    g.purse = 22;                                   // four $5 cards at most
    CHECK_EQ(g.maxCards(), 4);
    g.opt.stakes = 2;                               // $25 stakes fall to $10
    CHECK_EQ(g.priceNow(), 10);
    CHECK_EQ(g.maxCards(), 2);
    g.opt.stakes = 0;
    g.purse = 500; g.buyN = 3;
    auto ev = tick(g, A);
    Event e;
    CHECK(has(ev, Ev::Start, &e));
    CHECK_EQ(e.a, 3); CHECK_EQ(e.amount, 15);
    CHECK_EQ(g.purse, 485);
    // Nothing called yet: A is a miss, B has no power-up.
    ev = tick(g, A); CHECK(has(ev, Ev::Miss, &e)); CHECK_EQ(e.a, 0);
    ev = tick(g, B); CHECK(has(ev, Ev::Miss, &e)); CHECK_EQ(e.a, 1);
    // Swiping wraps.
    tick(g, RIGHT); CHECK_EQ(g.focus, 1);
    tick(g, RIGHT); tick(g, RIGHT); CHECK_EQ(g.focus, 0);
    tick(g, LEFT); CHECK_EQ(g.focus, 2);
    tick(g, RIGHT);
    // A number on card 1 only counts once daubed there.
    uint8_t n = g.cards[1][0];
    for (int i = 0; i < 25; i++) if (g.cards[0][i] == n) g.cards[0][i] = (uint8_t)(n == 1 ? 2 : n - 1);   // not on card 0
    g.force(n);
    for (int t = 0; t < 200 && !g.nCalled; t++) ev = tick(g);
    CHECK_EQ(g.nCalled, 1);
    CHECK(g.called(n));
    CHECK(g.pend[1] & 1u);
    uint32_t p0 = g.pend[0];
    ev = tick(g, A);                                // on card 0
    if (!p0) { CHECK(has(ev, Ev::Miss)); }
    CHECK(g.pend[1] & 1u);
    CHECK(!(g.daub[1] & 1u));
    tick(g, RIGHT);
    ev = tick(g, A);
    CHECK(has(ev, Ev::Daub, &e));
    CHECK_EQ(e.a, 1); CHECK_EQ(e.c, 1);             // fast
    CHECK(g.daub[1] & 1u);
    CHECK_EQ(g.pend[1], 0u);
    CHECK_EQ(g.streak, 1);
    CHECK_EQ(g.meter, 3);
    // A top row on card 1, daubed late: a bingo all the same.
    for (int c = 1; c < 5; c++) g.force(g.cards[1][c]);
    g.hallBall = 75;
    for (int t = 0; t < 1000 && g.nCalled < 5; t++) tick(g);
    CHECK_EQ(popcount(g.pend[1] & 0x1F), 4);
    for (int t = 0; t < 70; t++) tick(g);
    g.forceRare(1);
    int32_t purse = g.purse;
    ev = tick(g, A);
    CHECK(has(ev, Ev::Bingo, &e));
    CHECK_EQ(e.a, 1); CHECK_EQ(e.b, 0);
    CHECK(e.c & WIN_RARE);
    CHECK(e.c & WIN_JACKPOT);
    CHECK_EQ(g.purse, purse + e.amount);
    CHECK(g.phase == Phase::Won);
}

static void testPowers() {
    Bingo g;
    fresh(g, 4242);
    g.start(2);
    g.hallBall = 75;
    Event e;
    // WILD: the open cell on the line nearest done (a line through the centre).
    g.grant(P_WILD);
    auto ev = tick(g, B);
    CHECK(has(ev, Ev::PowerUse, &e));
    CHECK_EQ(e.a, P_WILD);
    CHECK_EQ(popcount((uint32_t)e.amount), 1);
    CHECK_EQ(popcount(g.daub[0]), 2);
    CHECK_EQ(g.power, P_NONE);
    // Four wilds along one line through the centre win.
    for (int i = 0; i < 3; i++) { g.grant(P_WILD); ev = tick(g, B); }
    CHECK(has(ev, Ev::Bingo));
    // FREEZE holds the caller for two intervals.
    fresh(g, 4243);
    g.start(1);
    for (int t = 0; t < 99; t++) tick(g);
    CHECK_EQ(g.nCalled, 0);
    g.grant(P_FREEZE);
    tick(g, B);
    for (int t = 0; t < 2 * 120 - 1; t++) tick(g);
    CHECK_EQ(g.nCalled, 0);
    tick(g);
    CHECK_EQ(g.nCalled, 1);
    // 2X doubles the pot on that card.
    fresh(g, 4244);
    g.start(2);
    g.hallBall = 75;
    tick(g, RIGHT);
    g.grant(P_DOUBLE);
    tick(g, B);
    CHECK_EQ(g.doubleCard, 1);
    for (int c = 0; c < 5; c++) g.force(g.cards[1][20 + c]);
    for (int t = 0; t < 2000 && g.nCalled < 5; t++) tick(g);
    for (int t = 0; t < 1500; t++) tick(g);         // past the jackpot window
    int32_t pot = g.pot;
    ev = tick(g, A);
    CHECK(has(ev, Ev::Bingo, &e));
    CHECK(e.c & WIN_DOUBLE);
    CHECK(!(e.c & WIN_JACKPOT));
    CHECK_EQ(e.amount, pot * 2);
    // The meter fills with daubs and grants a power-up.
    fresh(g, 4245);
    g.purse = 1000;
    g.start(9);
    g.hallBall = 75;
    bool granted = false;
    for (int t = 0; t < 40 * 120 && g.phase == Phase::Calling && !granted; t++) {
        ev = tick(g);
        if (has(ev, Ev::Call)) {
            g.daubAll();
            Event x;
            while (g.popEvent(x)) if (x.type == Ev::Power) granted = true;
        }
    }
    CHECK(granted || g.phase != Phase::Calling);
    if (granted) { CHECK(g.power >= 1 && g.power <= POWER_KINDS); CHECK_EQ(g.meter, 0); }
}

static void testSave() {
    for (uint32_t seed = 1; seed <= 200; seed++) {
        Bingo g;
        fresh(g, seed * 104729u);
        g.opt.speed = 1;
        g.start((uint8_t)(1 + seed % 9));
        int stop = 3 + (int)(seed % 9);
        for (int t = 0; t < 4000 && g.phase == Phase::Calling && g.nCalled < stop; t++) {
            auto ev = tick(g);
            if (has(ev, Ev::Call) && (seed & 1)) { g.daubAll(); Event e; while (g.popEvent(e)) {} }
        }
        if (g.phase != Phase::Calling) continue;
        RoundSave s;
        g.quitNow();
        CHECK(g.hasRound());
        g.saveRound(s);
        Bingo h;
        memset((void *)&h, 0, sizeof h);
        h.purse = g.purse;
        h.loadRound(s);
        CHECK(h.hasRound());
        h.resume();
        CHECK(h.phase == Phase::Calling);
        CHECK_EQ(h.nCards, g.nCards); CHECK_EQ(h.nCalled, g.nCalled);
        CHECK_EQ(h.hallBall, g.hallBall); CHECK_EQ(h.hallTable, g.hallTable);
        CHECK_EQ(h.pot, g.pot); CHECK_EQ(h.focus, g.focus); CHECK_EQ(h.meter, g.meter);
        CHECK(!memcmp(h.balls, g.balls, 75));
        CHECK(!memcmp(h.cards, g.cards, sizeof(g.cards[0]) * g.nCards));
        for (int k = 0; k < g.nCards; k++) { CHECK_EQ(h.daub[k], g.daub[k]); CHECK_EQ(h.pend[k], g.pend[k]); }
        for (int n = 1; n <= 75; n++) CHECK_EQ(h.called((uint8_t)n), g.called((uint8_t)n));
        // And the same session can pick it up again too.
        g.resume();
        CHECK(g.phase == Phase::Calling);
    }
}

static void testBroke() {
    Bingo g;
    fresh(g, 5);
    g.purse = 5;
    g.buyN = 1;
    tick(g, A);
    CHECK_EQ(g.purse, 0);
    g.hallBall = 4;
    bool over = false;
    for (int t = 0; t < 5000 && !over; t++) over = has(tick(g), Ev::GameOver);
    CHECK(over);
    CHECK(g.phase == Phase::GameLost);
    CHECK_EQ(g.stats.gamesBroke, 1);
    CHECK_EQ(g.dropped(), 0);
}

static void dump() {
    for (uint32_t seed = 1; seed <= 40; seed++) {
        static const int N[3] = {1, 4, 9}, R[3] = {2, 0, 1};      // hall option: 8, 20, 40 rivals
        for (int k = 0; k < 3; k++) {
            Bingo g;
            fresh(g, seed * 2654435761u);
            g.purse = 1000;
            g.opt.hall = (uint8_t)R[k];
            g.start((uint8_t)N[k]);
            int mine = 76;
            for (int c = 0; c < N[k]; c++) { int f = firstLine(g, g.cards[c]); if (f < mine) mine = f; }
            printf("%u %d %d hall=%d table=%d mine=%d balls=", (unsigned)seed, N[k], RIVALS[R[k]], g.hallBall, g.hallTable, mine);
            for (int i = 0; i < 12; i++) printf("%d%s", g.balls[i], i < 11 ? "," : "");
            printf(" last=");
            for (int i = 0; i < 25; i++) printf("%d%s", g.cards[N[k] - 1][i], i < 24 ? "," : "");
            printf("\n");
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--dump")) { dump(); return 0; }
    testLines();
    testSetup();
    testRace();
    testJackpotOdds();
    testButtons();
    testPowers();
    testSave();
    testBroke();
    printf("%ld checks, %ld failures\n", checks, failures);
    return failures ? 1 : 0;
}
