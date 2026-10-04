// Host tests of the rules: the board, the two modes turn by turn, saved
// games, the CPU's choice, and whole games by the thousand.
//
//     rpgame test [quick]
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../../Game.h"
#include "../../Cpu.h"

using namespace game;
using namespace layout;

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::vector<Event> seen;
static unsigned maxQueued;

// Run the game with a stage that is never busy, until it wants a player or ends.
static void settle() {
    for (int i = 0; i < 100; i++) {
        game::update(false);
        Event e;
        unsigned n = 0;
        while (popEvent(e)) { seen.push_back(e); n++; }
        if (n > maxQueued) maxQueued = n;
        if (!n && (phase() == P_OVER || humanToAct())) return;
    }
}

static void begin(uint8_t mode, uint8_t players = 2, uint8_t kind = HUMAN, uint32_t seed = 1) {
    Setup s = {{0, 0, 0, 0}, mode, seed};
    for (uint8_t i = 0; i < players; i++) s.kind[i] = kind;
    start(s);
    seen.clear();
    settle();
}

static const Event *find(uint8_t type, int nth = 0) {
    for (auto &e : seen) if (e.type == type && !nth--) return &e;
    return nullptr;
}

static void throwDice(uint8_t d1, uint8_t d2 = 0) {
    seen.clear();
    forceDice(d1, d2);
    CHECK(roll());
    settle();
}

// ---------------------------------------------------------------------------
static void testLayout() {
    bool used[101] = {};
    for (uint8_t i = 0; i < LINKS; i++) {
        const Link &l = LINK[i];
        CHECK(l.from > 1 && l.from < LAST && l.to >= 1 && l.to < LAST);
        CHECK(i < LADDERS ? l.to > l.from : l.to < l.from);
        CHECK(row(l.from) != row(l.to));                // nothing lies along a row
        CHECK(!used[l.from] && !used[l.to]);            // one thing to a square, so no chains
        used[l.from] = used[l.to] = true;
        CHECK(linkAt(l.from) == i);
    }
    // No six squares in a row all snake heads (a wall nobody can pass).
    int heads = 0, run = 0, worst = 0;
    for (uint8_t n = 1; n <= LAST; n++) {
        int k = linkAt(n);
        bool head = k >= LADDERS;
        run = head ? run + 1 : 0;
        if (run > worst) worst = run;
        if (head && n >= 94) heads++;
    }
    CHECK(worst < 3);
    CHECK(heads <= 2);
    // The winding: 1 bottom left, 10 bottom right, 11 above it, 100 top left.
    CHECK(row(1) == 0 && col(1) == 0 && col(10) == 9 && row(11) == 1 && col(11) == 9 && col(20) == 0);
    CHECK(row(100) == 9 && col(100) == 0 && col(91) == 9);
    for (uint8_t n = 11; n <= LAST; n++) CHECK(row(below(n)) == row(n) - 1 && col(below(n)) == col(n));
    for (uint8_t n = 1; n <= 10; n++) CHECK(below(n) == 1);
    // landing() against a plain walk.
    for (int from = 1; from < 100; from++)
        for (int s = 0; s <= 6; s++) {
            int n = from, dir = 1;
            for (int i = 0; i < s; i++) { if (n == 100) dir = -1; n += dir; }
            uint8_t via, to = landing((uint8_t)from, (uint8_t)s, &via);
            CHECK(via == n);
            int k = linkAt((uint8_t)n);
            CHECK(to == (k < 0 ? n : LINK[k].to));
        }
}

static void testClassic() {
    begin(CLASSIC);
    CHECK(st.players == 2 && st.pos[0] == 1 && st.pos[1] == 1 && st.cur == 0 && phase() == P_ROLL);
    CHECK(find(EV_START) && find(EV_TURN) && find(EV_TURN)->b == 1);
    throwDice(1);                                       // 1 -> 2
    CHECK(st.pos[0] == 2 && st.cur == 1);
    const Event *m = find(EV_MOVE);
    CHECK(m && m->a == 0 && m->b == 1 && m->c == 2 && m->amount == 1);
    CHECK(find(EV_DICE)->b == 0 && find(EV_DICE)->c == 0);
    throwDice(2);                                       // 1 -> 3, the ladder to 24
    CHECK(st.pos[1] == 24 && st.ladders[1] == 1 && find(EV_LADDER) && find(EV_LADDER)->c == 24);
    CHECK(st.cur == 0);
    // A six: again, twice at most.
    st.pos[0] = 10;
    throwDice(6);
    CHECK(st.pos[0] == 16 && st.cur == 0 && st.streak == 1 && find(EV_DICE)->c == 1 && find(EV_TURN)->c == 1);
    throwDice(6);
    CHECK(st.pos[0] == 22 && st.cur == 0 && st.streak == 2);
    throwDice(6);                                       // the third six only moves
    CHECK(st.pos[0] == 28 && st.cur == 1 && find(EV_DICE)->c == 0);
    // Squares are shared: nobody is bumped.
    st.pos[1] = 27;
    throwDice(1);
    CHECK(st.pos[1] == 28 && st.pos[0] == 28 && !find(EV_BUMP));
    // A snake.
    st.pos[0] = 24;
    throwDice(2);
    CHECK(st.pos[0] == 5 && st.snakes[0] == 1 && find(EV_SNAKE)->b == 26);
    // Past 100 and back - onto a snake.
    st.pos[1] = 97;
    throwDice(4);
    m = find(EV_MOVE);
    CHECK(m->b == 97 && m->c == 99 && m->amount == 4);
    CHECK(st.pos[1] == 61 && find(EV_SNAKE));
    // The exact roll wins, and ends everything.
    st.pos[0] = 98;
    throwDice(2);
    CHECK(st.over && st.winner == 0 && phase() == P_OVER && find(EV_OVER) && find(EV_OVER)->a == 0);
    CHECK(!roll());
    // A six onto 100 wins, with no roll after it.
    begin(CLASSIC);
    st.pos[0] = 94;
    throwDice(6);
    CHECK(st.over && phase() == P_OVER);
}

static void testArcade() {
    begin(ARCADE, 3);
    throwDice(2, 5);
    CHECK(phase() == P_PICK && humanToAct() && die(0) == 2 && die(1) == 5 && st.pos[0] == 1);
    CHECK(!roll() && !pick(2));
    seen.clear();
    CHECK(pick(1));
    settle();
    CHECK(st.pos[0] == 6 && st.cur == 1);
    // Doubles: no choice, and again.
    throwDice(3, 3);
    CHECK(st.pos[1] == 4 && st.cur == 1 && st.streak == 1 && phase() == P_ROLL);
    throwDice(1, 1);
    throwDice(2, 2);                                    // the third pair only moves
    CHECK(st.pos[1] == 7 && st.cur == 2);
    // A bump: the rival drops a row.
    st.pos[0] = 36; st.pos[2] = 35;
    throwDice(1, 6);
    pick(0);
    seen.clear();
    settle();
    const Event *b = find(EV_BUMP);
    CHECK(b && b->a == 0 && b->b == 36 && b->c == 25 && b->amount == 2);
    CHECK(st.pos[2] == 36 && st.pos[0] == 25 && st.bumps[2] == 1);
    // Bumped onto a snake's head (26, from 35), and two at once.
    st.cur = 2; st.pos[0] = st.pos[1] = 35; st.pos[2] = 33;
    seen.clear();
    forceDice(2, 1); roll(); pick(0); settle();
    CHECK(st.pos[2] == 35 && st.pos[0] == 5 && st.pos[1] == 5 && st.bumps[2] == 3);
    CHECK(find(EV_BUMP, 1) && find(EV_SNAKE, 1) && st.snakes[0] == 1 && st.snakes[1] == 1);
    // From the bottom row: back to square 1, where nobody is ever bumped.
    st.cur = 0; st.pos[0] = 4; st.pos[1] = 7; st.pos[2] = 1;
    seen.clear();
    forceDice(3, 1); roll(); pick(0); settle();
    CHECK(st.pos[0] == 7 && st.pos[1] == 1 && st.pos[2] == 1);
    // Riding a snake onto a rival bumps it too.
    st.cur = 0; st.pos[0] = 25; st.pos[1] = 5; st.pos[2] = 51;
    seen.clear();
    forceDice(1, 2); roll(); pick(0); settle();
    CHECK(st.pos[0] == 5 && st.pos[1] == 1);
    // A win comes before any bump or second roll.
    st.cur = 1; st.pos[1] = 97;
    throwDice(3, 3);
    CHECK(st.over && st.winner == 1 && !find(EV_BUMP));
}

static void testSave() {
    begin(ARCADE, 2, HUMAN, 77);
    throwDice(2, 5); pick(1); settle();
    throwDice(4, 4);                                    // seat 1, mid-turn after a pair
    State saved = checkpoint();
    CHECK(saved.cur == 1 && saved.streak == 0 && saved.pos[0] == 6 && saved.pos[1] == 1 && st.pos[1] == 5);
    State live = st;
    CHECK(restore(saved));
    seen.clear();
    settle();
    CHECK(find(EV_START) && phase() == P_ROLL && st.cur == 1 && st.turn == live.turn);
    throwDice(4, 4);
    CHECK(memcmp(&st, &live, sizeof st) == 0);          // the same dice play the same turn
    State bad = saved;
    bad.players = 5; CHECK(!restore(bad));
    bad = saved; bad.pos[0] = 0; CHECK(!restore(bad));
    bad = saved; bad.pos[1] = 100; CHECK(!restore(bad));
    bad = saved; bad.kind[1] = 9; CHECK(!restore(bad));
    bad = saved; bad.over = 1; CHECK(!restore(bad));
    CHECK(sizeof(State) <= 64);
}

// One whole game; returns the winner, or -1 if it never ended.
static int play(uint8_t mode, const uint8_t kinds[4], uint32_t seed, int *turns = nullptr) {
    Setup s = {{kinds[0], kinds[1], kinds[2], kinds[3]}, mode, seed};
    start(s);
    for (int i = 0; i < 200000 && phase() != P_OVER; i++) {
        game::update(false);
        Event e;
        unsigned n = 0;
        while (popEvent(e)) n++;
        if (n > maxQueued) maxQueued = n;
        for (uint8_t p = 0; p < st.players; p++)
            if (st.pos[p] < 1 || st.pos[p] > 100 || (linkAt(st.pos[p]) >= 0 && phase() != P_OVER)) return -2;
        if (humanToAct()) {                             // a "human": rolls, and picks by the seed
            if (phase() == P_ROLL) roll();
            else pick((uint8_t)((st.rng >> 3) & 1));
        }
    }
    if (turns) *turns = st.turn;
    return st.over && st.pos[st.winner] == 100 ? st.winner : -1;
}

static void testCpu() {
    // Purity: asking does not change the game.
    begin(ARCADE, 2, CPU + 2, 5);
    st.pos[0] = 30; st.pos[1] = 33;
    State before = st;
    uint8_t a = cpu::pick(0, 3, 5);
    CHECK(memcmp(&before, &st, sizeof st) == 0 && cpu::pick(0, 3, 5) == a);
    // Every level takes the win.
    for (uint8_t lv = 0; lv < LEVELS; lv++) {
        st.kind[0] = (uint8_t)(CPU + lv);
        st.pos[0] = 95;
        CHECK(cpu::pick(0, 5, 2) == 0 && cpu::pick(0, 2, 5) == 1);
    }
    // FAIR goes furthest: from 20, a 1 (the ladder at 21) over a 6 (the snake at 26).
    st.kind[0] = CPU + 1; st.pos[0] = 20; st.pos[1] = 2;
    CHECK(cpu::pick(0, 6, 1) == 1 && cpu::pick(0, 1, 6) == 0);
    // SHARK bumps the leader rather than go one square further.
    st.kind[0] = CPU + 2; st.pos[0] = 80; st.pos[1] = 83;
    CHECK(cpu::pick(0, 3, 4) == 0);
}

// The SHARK's table against the board as it is: the same sums as tools/turns.py.
static void testTurns() {
    double e[101] = {};
    for (int it = 0; it < 2000; it++)
        for (int n = 99; n >= 1; n--) {
            double s = 0;
            for (int a = 1; a <= 6; a++)
                for (int b = 1; b <= 6; b++) {
                    double x = e[landing((uint8_t)n, (uint8_t)a)], y = e[landing((uint8_t)n, (uint8_t)b)];
                    s += a == b && x > 0 ? x - 1 : x < y ? x : y;
                }
            e[n] = 1 + s / 36;
        }
    int worst = 0;
    for (int n = 1; n <= 100; n++) {
        int d = (int)(e[n] * 8 + 0.5) - cpu::TURNS[n];
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    CHECK(worst <= 1);                                  // else: python tools/turns.py, into Cpu.cpp
    printf("  best play from square 1: %.1f turns\n", e[1]);
}

static void testStrength(int games) {
    // Head to head, taking turns to go first.
    static const char *const NAME[3] = {"EASY", "FAIR", "SHARK"};
    for (int hi = 1; hi < 3; hi++) {
        int lo = hi - 1, wins = 0;
        for (int g = 0; g < games; g++) {
            uint8_t k[4] = {(uint8_t)(CPU + (g & 1 ? lo : hi)), (uint8_t)(CPU + (g & 1 ? hi : lo)), 0, 0};
            int w = play(ARCADE, k, 1000u + (uint32_t)g * 7919u);
            CHECK(w >= 0);
            wins += w == (g & 1);
        }
        printf("  %s beats %s in %.1f%% of %d games\n", NAME[hi], NAME[lo], 100.0 * wins / games, games);
        CHECK(wins * 100 > games * 52);
    }
}

static void testFuzz(int games) {
    long total[2] = {0, 0};
    int longest = 0, n[2] = {0, 0};
    for (int g = 0; g < games; g++) {
        uint8_t mode = (uint8_t)(g & 1), players = (uint8_t)(2 + g / 2 % 3), k[4] = {0, 0, 0, 0};
        for (uint8_t i = 0; i < players; i++) k[i] = (uint8_t)(1 + (g / 6 + i) % 4);
        int turns = 0, w = play(mode, k, 0xC0FFEEu + (uint32_t)g * 2654435761u, &turns);
        CHECK(w >= 0 && w < players);
        total[mode] += turns; n[mode]++;
        if (turns > longest) longest = turns;
    }
    printf("  %d games: CLASSIC %.0f turns a game, ARCADE %.0f, the longest %d\n", games,
           (double)total[0] / n[0], (double)total[1] / n[1], longest);
    CHECK(longest < 1000);
    CHECK(maxQueued <= 12);
    printf("  most events in one step: %u\n", maxQueued);
}

int main(int argc, char **argv) {
    bool quick = argc > 1;
    testLayout();
    testClassic();
    testArcade();
    testSave();
    testCpu();
    testTurns();
    testStrength(quick ? 2000 : 20000);
    testFuzz(quick ? 5000 : 100000);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
