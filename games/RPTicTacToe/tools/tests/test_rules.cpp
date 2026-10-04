// Host tests: every table's rules, the dealer, the match flow.
#include <stdio.h>
#include <string.h>
#include "../../Rules.h"
#include "../../Cpu.h"
#include "../../Match.h"
#include "../../Text.h"

static int checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { failures++; printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static void moves(Board &b, const char *seq) {       // "40 81 ..." digit pairs: cell, arg
    for (const char *p = seq; *p; p++) {
        if (*p == ' ') continue;
        uint8_t cell = (uint8_t)(*p - '0'), arg = (uint8_t)(p[1] - '0');
        p++;
        if (!rules::legal(b, cell, arg)) { failures++; printf("FAIL illegal %d,%d in \"%s\"\n", cell, arg, seq); return; }
        rules::play(b, cell, arg);
    }
}

static void testClassic() {
    Board b;
    rules::start(b, M_CLASSIC);
    CHECK_EQ(b.n, 9);
    moves(b, "00 30 10 40");
    CHECK_EQ(b.result, R_NONE);
    CHECK(!rules::legal(b, 0, 0));
    moves(b, "20");
    CHECK_EQ(b.result, R_P0);
    CHECK_EQ(b.win[0], 0); CHECK_EQ(b.win[2], 2);
    CHECK(!rules::legal(b, 8, 0));
    rules::start(b, M_CLASSIC);
    moves(b, "00 10 20 40 30 60 50 80 70");            // X O X / X O X / O X O
    CHECK_EQ(b.result, R_DRAW);
    rules::start(b, M_CLASSIC);
    moves(b, "00 20 10 40 80 60");                     // O takes the anti-diagonal
    CHECK_EQ(b.result, R_P1);
}

static void testMisere() {
    Board b;
    rules::start(b, M_MISERE);
    moves(b, "00 30 10 40 20");
    CHECK_EQ(b.result, R_P1);                          // the player lined up three: he loses
}

static void testVanish() {
    Board b;
    rules::start(b, M_VANISH);
    moves(b, "00 10 40 80 20 60");                     // X: 0 4 2, O: 1 8 6
    CHECK_EQ(b.result, R_NONE);
    CHECK_EQ(b.left, 3);
    moves(b, "30");                                    // X's fourth: 0 goes
    CHECK_EQ(b.gone, 0);
    CHECK_EQ(b.cell[0], 0);
    CHECK_EQ(b.left, 3);
    CHECK(rules::legal(b, 0, 0));
    moves(b, "70");                                    // O: 8 6 7 - a row, and 1 goes
    CHECK_EQ(b.gone, 1);
    CHECK_EQ(b.result, R_P1);
    // The vanished mark does not count: X 0,1 then far cells, then 2 after 0 went.
    rules::start(b, M_VANISH);
    moves(b, "00 30 10 40 80 60 20");                  // X: 0 1 8 then 2 -> 0 vanishes: no row
    CHECK_EQ(b.result, R_NONE);
}

static void testGobble() {
    Board b;
    rules::start(b, M_GOBBLE);
    moves(b, "00");                                    // small X on 0
    CHECK(!rules::legal(b, 0, 0));                     // a small can't cover a small
    CHECK(rules::legal(b, 0, 1));
    moves(b, "01");                                    // medium O covers it
    CHECK_EQ(topOf(b.cell[0]), 2);
    CHECK_EQ(b.stock[1][1], 1);
    moves(b, "02");                                    // large X covers that
    CHECK_EQ(topOf(b.cell[0]), 1);
    CHECK(!rules::legal(b, 0, 2));
    moves(b, "40 11 50 22");                           // X: 0(L) 1(M) 2(L)
    CHECK_EQ(b.result, R_P0);
    rules::start(b, M_GOBBLE);
    moves(b, "00 10 01");
    CHECK_EQ(b.stock[0][0], 1); CHECK_EQ(b.stock[0][1], 1);
    moves(b, "12 02");
    CHECK(!rules::legal(b, 3, 2) || b.stock[b.turn][2]);
}

static void testWild() {
    Board b;
    rules::start(b, M_WILD);
    moves(b, "00 11 31 21");                           // X O . / O . . : dealer plays O on 2? no line yet
    CHECK_EQ(b.result, R_NONE);
    moves(b, "40 80");                                 // X on 4 (player), X on 8 (dealer): 0 4 8
    CHECK_EQ(b.result, R_P1);                          // whoever completes it
}

static void testBig() {
    Board b;
    rules::start(b, M_BIG5);
    moves(b, "00 50 10 60 20 70");
    CHECK_EQ(b.result, R_NONE);
    rules::play(b, 3, 0);
    CHECK_EQ(b.result, R_P0);
    CHECK_EQ(b.win[0], 0); CHECK_EQ(b.win[3], 3); CHECK_EQ(b.win[4], NONE);
    // No wrap from one row's end to the next row's start.
    rules::start(b, M_BIG5);
    b.cell[3] = b.cell[4] = b.cell[5] = 1; b.left -= 3;
    rules::play(b, 6, 0);
    CHECK_EQ(b.result, R_NONE);

    rules::start(b, M_99);
    CHECK_EQ(b.n, 99);
    for (int i = 0; i < 4; i++) { rules::play(b, (uint8_t)(12 * i), 0); rules::play(b, (uint8_t)(90 + i), 0); }
    CHECK_EQ(b.result, R_NONE);
    rules::play(b, 48, 0);                             // the diagonal 0 12 24 36 48
    CHECK_EQ(b.result, R_P0);
    CHECK_EQ(b.run, 5);
}

static void testUltimate() {
    Board b;
    rules::start(b, M_ULTIMATE);
    CHECK_EQ(b.must, NONE);
    rules::play(b, 40, 0);                             // the very centre: local cell 4 -> board 4
    CHECK_EQ(b.must, 4);
    CHECK(!rules::legal(b, 0, 0));
    CHECK(rules::legal(b, 30, 0));                     // row 3, col 3: board 4
    rules::play(b, 30, 0);                             // local 0 -> board 0
    CHECK_EQ(b.must, 0);
    // X takes board 0 with its top row.
    rules::start(b, M_ULTIMATE);
    b.cell[0] = b.cell[1] = 1;
    rules::play(b, 2, 0);
    CHECK_EQ(b.small[0], 1);
    CHECK_EQ(b.smallWon, 0);
    CHECK_EQ(b.must, 2);
    CHECK_EQ(b.result, R_NONE);
    CHECK(!rules::legal(b, 10, 0));                    // board 0 is closed
    // Sent to a closed board: anywhere open.
    b.turn = 1;
    rules::play(b, 60, 0);                             // row 6, col 6: board 8, local 0 -> board 0 (closed)
    CHECK_EQ(b.must, NONE);
    // Three boards in a row.
    rules::start(b, M_ULTIMATE);
    b.small[0] = b.small[1] = 1;
    b.cell[6] = b.cell[7] = 1;
    rules::play(b, 8, 0);
    CHECK_EQ(b.result, R_P0);
    CHECK_EQ(b.win[0], smallCentre(0)); CHECK_EQ(b.win[2], smallCentre(2)); CHECK_EQ(b.win[3], NONE);
    CHECK_EQ(smallCentre(0), 10); CHECK_EQ(smallCentre(8), 70); CHECK_EQ(smallOf(40), 4); CHECK_EQ(smallOf(80), 8);
}

static void testNewTables() {
    Board b;
    // ALL X: everyone plays X; the one who completes a row loses.
    rules::start(b, M_ALLX);
    moves(b, "00 10");
    CHECK_EQ(b.cell[1], 1);
    moves(b, "20");
    CHECK_EQ(b.result, R_P1);
    // WRAP: 3,4,0 and 1 on the top row of the 5x5 make four.
    rules::start(b, M_WRAP);
    b.cell[3] = b.cell[4] = b.cell[0] = 1; b.left -= 3;
    rules::play(b, 1, 0);
    CHECK_EQ(b.result, R_P0);
    CHECK_EQ(b.win[0], 3); CHECK_EQ(b.win[3], 1);
    rules::start(b, M_WRAP);                           // a diagonal over the corner
    b.cell[18] = b.cell[24] = b.cell[0] = 1; b.left -= 3;
    rules::play(b, 6, 0);
    CHECK_EQ(b.result, R_P0);
    // DROP 4: only the lowest empty cell of a column.
    rules::start(b, M_DROP);
    CHECK(!rules::legal(b, 3, 0));
    CHECK_EQ(rules::drop(b, 3), 38);
    CHECK(rules::legal(b, 38, 0));
    rules::play(b, 38, 0);
    CHECK_EQ(rules::drop(b, 3), 31);
    CHECK(rules::legal(b, 31, 0));
    for (int i = 0; i < 5; i++) rules::play(b, rules::drop(b, 3), 0);
    CHECK_EQ(rules::drop(b, 3), NONE);
    // MINES: the mark is lost, the cell is dead, the turn passes.
    rules::start(b, M_MINES);
    b.mines = 1u << 12;
    rules::play(b, 12, 0);
    CHECK_EQ(b.boom, 1);
    CHECK_EQ(b.cell[12], 3);
    CHECK_EQ(b.turn, 1);
    CHECK(!rules::legal(b, 12, 0));
    b.cell[10] = b.cell[11] = b.cell[13] = 2;          // a dead cell breaks a line
    rules::play(b, 14, 0);
    CHECK_EQ(b.result, R_NONE);
    // DARK through the match: walking into a hidden mark shows it and keeps the turn.
    Match mt;
    mt.seed(5);
    mt.start(M_DARK, 2);
    while (mt.phase != Phase::Human) mt.update(0, 0, false);
    mt.b.cell[4] = 2; mt.b.left--;
    mt.cur = 4;
    mt.update(K_A, 0, false);
    CHECK_EQ(mt.nEv, 1); CHECK_EQ(mt.ev[0].type, EV_BUMP);
    CHECK(mt.phase == Phase::Human);
    CHECK_EQ(mt.b.seen, 1 << 4);
    mt.update(K_A, 0, false);
    CHECK_EQ(mt.ev[0].type, EV_DENY);
    // Two players: the second side is a human turn, and the score is kept.
    mt.score[0] = mt.score[1] = 0;
    mt.start(M_CLASSIC, 0, true);
    static const uint8_t SEQ[5] = {0, 3, 1, 4, 2};
    for (int i = 0; i < 5; i++) {
        while (mt.phase != Phase::Human) mt.update(0, 0, false);
        CHECK_EQ(mt.b.turn, i & 1);
        mt.cur = SEQ[i];
        mt.update(K_A, 0, false);
    }
    while (mt.phase != Phase::Over) mt.update(0, 0, false);
    CHECK_EQ(mt.result, R_P0);
    CHECK_EQ(mt.score[0], 1); CHECK_EQ(mt.score[1], 0);
}

// A whole game: the dealer's brain for side 1, uniform random for side 0.
// Returns the result; plies counts the moves.
static uint8_t playout(Mode m, uint8_t level, uint32_t &rng, bool bothCpu, int &plies) {
    Board b;
    rules::start(b, m);
    if (b.flags & F_MINES) while (__builtin_popcount(b.mines) < MINE_COUNT) b.mines |= 1u << (cpu::rnd(&rng) % b.n);
    plies = 0;
    while (!b.result && plies < 600) {
        if (b.flags & (F_COIN | F_AUCTION)) b.turn = (uint8_t)(cpu::rnd(&rng) >> 9 & 1);
        cpu::Move mv;
        if (b.turn == 1 || bothCpu) {
            cpu::begin(b, b.turn == 1 ? level : 2, &rng);
            int guard = 0;
            while (!cpu::step(b, mv)) if (++guard > 100) break;
        } else {
            do { mv.cell = (uint8_t)(cpu::rnd(&rng) % b.n); mv.arg = (uint8_t)(cpu::rnd(&rng) % 3); }
            while (!rules::legal(b, mv.cell, mv.arg));
        }
        if (!rules::legal(b, mv.cell, mv.arg)) { failures++; printf("FAIL cpu illegal move mode %d\n", m); return R_NONE; }
        rules::play(b, mv.cell, mv.arg);
        plies++;
    }
    return b.result;
}

static void testDealer(bool table) {
    uint32_t rng = 12345;
    if (table) printf("dealer (side 1) against a random player: lost%% / drawn%% / won%%, mean plies\n");
    for (int m = 0; m < MODE_COUNT; m++) {
        for (uint8_t level = 0; level < 3; level++) {
            int n = (m >= M_BIG5) ? 60 : 300, res[4] = {0, 0, 0, 0}, total = 0;
            for (int g = 0; g < n; g++) {
                int plies;
                uint8_t r = playout((Mode)m, level, rng, false, plies);
                res[r]++;
                total += plies;
                CHECK(r != R_NONE || m == M_VANISH);             // every game ends (VANISH may run long)
            }
            if (m == M_VANISH) CHECK_EQ(res[R_DRAW], 0);
            if (level == 2 && m == M_CLASSIC) CHECK_EQ(res[R_P0], 0);   // the shark never loses the classic
            if (level == 2 && m >= M_BIG5) CHECK(res[R_P1] * 10 >= n * (m == M_MINES ? 7 : 9));
            if (level == 2) CHECK(res[R_P1] > res[R_P0]);
            if (table) printf("  %-10s L%d  %3d / %3d / %3d   %d\n", MODE_NAME[m], level, res[R_P0] * 100 / n,
                              res[R_DRAW] * 100 / n, res[R_P1] * 100 / n, total / n);
        }
    }
    // Shark against shark on the classic board: always a draw.
    for (int g = 0; g < 50; g++) {
        int plies;
        CHECK_EQ(playout(M_CLASSIC, 2, rng, true, plies), R_DRAW);
    }
}

// The match flow under random button mashing: every table reaches Over.
static void testMatch() {
    uint32_t rng = 777;
    for (int m = 0; m < MODE_COUNT; m++) {
        for (int g = 0; g < 20; g++) {
            Match mt;
            mt.seed(cpu::rnd(&rng));
            mt.start((Mode)m, (uint8_t)(g % 3));
            long ticks = 0;
            int places = 0;
            while (mt.phase != Phase::Over && ticks < 400000) {
                uint8_t r = (uint8_t)(cpu::rnd(&rng) >> 8), pressed = 0;
                if ((r & 3) == 0) pressed = (uint8_t)(1u << (cpu::rnd(&rng) % 6));
                mt.update(pressed, pressed, false);
                for (uint8_t i = 0; i < mt.nEv; i++) if (mt.ev[i].type == EV_PLACE) places++;
                ticks++;
            }
            CHECK(mt.phase == Phase::Over);
            CHECK(mt.result != R_NONE);
            CHECK(places > 0);
            if (m == M_BLITZ) CHECK(ticks >= 600 && ticks < 12000);
            if (m == M_AUCTION) CHECK_EQ(mt.b.chips[0] + mt.b.chips[1], 2 * AUCTION_CHIPS);
        }
    }
    Match mt;
    mt.mode = M_BIG5; mt.level = 0; mt.result = R_P0; mt.b.flags = 0;
    CHECK_EQ(mt.payout(10, 0), 30);                    // 2:1
    CHECK_EQ(mt.payout(10, 2), 40);                    // + half again on a streak of two
    mt.level = 2;
    CHECK_EQ(mt.payout(10, 0), 70);                    // the shark pays triple
    mt.result = R_DRAW;
    CHECK_EQ(mt.payout(10, 3), 10);
    mt.result = R_P1;
    CHECK_EQ(mt.payout(10, 3), 0);
    mt.b.flags = F_BLITZ; mt.wins = 3;
    CHECK_EQ(mt.payout(20, 0), 30);
    for (int m = 0; m < MODE_COUNT; m++) {             // the dealer's bubble: 5 lines of 17
        int lines = 1, len = 0;
        for (const char *p = MODE_RULES[m]; *p; p++) {
            if (*p == '\n') { lines++; len = 0; } else { len++; CHECK(len <= 17); }
        }
        CHECK(lines <= 5);
    }
}

int main(int argc, char **argv) {
    testClassic();
    testMisere();
    testVanish();
    testGobble();
    testWild();
    testBig();
    testUltimate();
    testNewTables();
    testDealer(argc > 1);
    testMatch();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
