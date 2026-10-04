// Host tests: the rules against a second, naive implementation; whole
// matches through the game's own calls; the table layout; save and reload;
// the CPU's levels against each other.
//
//     rpgame test [--quick]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../../Dominoes.h"
#include "../../Ai.h"
#include "../../Match.h"
#include "../../Layout.h"

static long checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; if (failures < 20) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { failures++; \
    if (failures < 20) printf("FAIL %s:%d: %s = %ld, %s = %ld\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)

using namespace dom;

// ---------------------------------------------------------------------------
// The naive rules: the line kept as lists of tiles, everything worked out
// from them each time.
// ---------------------------------------------------------------------------
struct Naive {
    struct T { int a, b; };          // a faces the middle, b outward
    bool any = false, fives = false;
    T first{};
    std::vector<T> arm[4];

    bool firstDouble() const { return first.a == first.b; }
    bool sides() const { return fives && firstDouble() && !arm[E].empty() && !arm[W].empty(); }
    int open(int a) const {
        if (!arm[a].empty()) return arm[a].back().b;
        return a == W ? first.a : first.b;
    }
    int arms(int lo, int hi) const {
        if (!any) return 1;
        int m = 0;
        for (int a = 0; a < 4; a++) {
            if (a >= N && !sides()) continue;
            if (open(a) == lo || open(a) == hi) m |= 1 << a;
        }
        return m;
    }
    int sum() const {
        if (!any) return 0;
        int s = 0;
        if (arm[E].empty() && arm[W].empty()) return first.a + first.b;
        for (int a = 0; a < 2; a++) {
            if (arm[a].empty()) s += firstDouble() ? first.a + first.b : open(a);
            else { const T &t = arm[a].back(); s += t.a == t.b ? t.a + t.b : t.b; }
        }
        for (int a = 2; a < 4; a++)
            if (!arm[a].empty()) { const T &t = arm[a].back(); s += t.a == t.b ? t.a + t.b : t.b; }
        return s;
    }
    void put(int lo, int hi, int a) {
        if (!any) { any = true; first = {lo, hi}; return; }
        int v = open(a);
        arm[a].push_back(lo == v ? T{lo, hi} : T{hi, lo});
    }
};

static void testTiles() {
    uint32_t seen = 0;
    for (int b = 0; b <= 6; b++)
        for (int a = 0; a <= b; a++) {
            uint8_t t = tileOf((uint8_t)a, (uint8_t)b);
            CHECK(t < TILES);
            CHECK_EQ(lo(t), a);
            CHECK_EQ(hi(t), b);
            CHECK_EQ(tileOf((uint8_t)b, (uint8_t)a), t);
            seen |= 1u << t;
        }
    CHECK_EQ(seen, ALL);
    CHECK_EQ(handPips(ALL), 168);
    CHECK_EQ(count(ALL), 28);
    CHECK_EQ(nth(0x15, 2), 4);
    CHECK_EQ(nth(0x15, 3), NONE);
}

// Known positions of ALL FIVES.
static void testScoring() {
    Round r;
    uint8_t order[TILES];
    for (int i = 0; i < TILES; i++) order[i] = (uint8_t)i;
    deal(r, FIVES, order, 0);
    r.hand[0] = ALL; r.hand[1] = r.bone = 0;
    CHECK_EQ(place(r, 0, tileOf(5, 5), 0), 10);             // 5-5 set: ten
    CHECK_EQ(r.spinner, 1);
    CHECK_EQ(armsFor(r, tileOf(5, 0)), 3);
    CHECK_EQ(place(r, 0, tileOf(5, 0), E), 10);             // 5-5 | 5-0: the double still counts, 10 + 0
    CHECK_EQ(place(r, 0, tileOf(5, 6), W), 0);              // 6 .. 0: six
    CHECK_EQ(endSum(r), 6);
    CHECK_EQ(armsFor(r, tileOf(5, 4)), 12);                 // now the spinner's sides are open
    CHECK_EQ(place(r, 0, tileOf(5, 4), N), 10);             // 6 + 0 + 4
    CHECK_EQ(place(r, 0, tileOf(6, 6), W), 0);              // 12 + 0 + 4
    CHECK_EQ(endSum(r), 16);
    CHECK_EQ(place(r, 0, tileOf(5, 3), S), 0);              // 12 + 0 + 4 + 3
    CHECK_EQ(endSum(r), 19);
    CHECK_EQ(place(r, 0, tileOf(0, 1), E), 20);             // 12 + 1 + 4 + 3
    // A first tile that is no double: both its ends count, and there is no spinner.
    deal(r, FIVES, order, 0);
    r.hand[0] = ALL;
    CHECK_EQ(place(r, 0, tileOf(6, 4), 0), 10);
    CHECK_EQ(r.spinner, 0);
    CHECK_EQ(place(r, 0, tileOf(4, 4), W), 0);              // 6 + 8
    CHECK_EQ(endSum(r), 14);
    CHECK_EQ(place(r, 0, tileOf(6, 1), E), 0);              // 1 + 8
    CHECK_EQ(armsFor(r, tileOf(4, 2)), 2);
    // DRAW scores nothing in play.
    deal(r, DRAW, order, 0);
    r.hand[0] = ALL;
    CHECK_EQ(place(r, 0, tileOf(5, 5), 0), 0);
    CHECK_EQ(r.spinner, 0);
    // The round's end.
    uint8_t w;
    memset(&r, 0, sizeof r);
    r.game = FIVES; r.n = 1;
    r.hand[1] = (1u << tileOf(6, 6)) | (1u << tileOf(1, 0));    // 13 pips: 15 in fives
    CHECK_EQ(settle(r, DOMINO, w), 15);
    CHECK_EQ(w, 0);
    r.game = DRAW;
    CHECK_EQ(settle(r, DOMINO, w), 13);
    r.hand[0] = 1u << tileOf(2, 2);
    CHECK_EQ(settle(r, BLOCKED, w), 9);
    CHECK_EQ(w, 0);
    r.hand[0] = (1u << tileOf(6, 5)) | (1u << tileOf(1, 1));
    CHECK_EQ(settle(r, BLOCKED, w), 0);
    CHECK_EQ(w, 2);
}

// Random rounds, each step checked against the naive rules.
static void testAgainstNaive(int games) {
    Rng rng;
    rng.seed(1234, 9);
    for (int g = 0; g < games; g++) {
        uint8_t order[TILES];
        shuffle(order, rng);
        Round r;
        deal(r, g & 1 ? FIVES : DRAW, order, (uint8_t)(g & 2 ? 1 : 0));
        Naive nv;
        nv.fives = r.game == FIVES;
        uint8_t why;
        int steps = 0;
        while (!over(r, why) && steps++ < 200) {
            uint8_t s = r.turn;
            // Every tile: the same arms as the naive rules.
            std::vector<std::pair<int, int>> moves;
            for (uint8_t t = 0; t < TILES; t++) {
                int m = armsFor(r, t);
                CHECK_EQ(m, nv.arms(lo(t), hi(t)));
                if (!((r.hand[s] >> t) & 1)) continue;
                for (int a = 0; a < 4; a++) if ((m >> a) & 1) moves.push_back({t, a});
                uint8_t arms[ARMS], k = options(r, t, arms);
                CHECK((k != 0) == (m != 0));
                for (uint8_t i = 0; i < k; i++) CHECK((m >> arms[i]) & 1);
            }
            CHECK_EQ(canPlay(r, s), !moves.empty());
            if (moves.empty()) {
                if (r.bone) drew(r, s, nth(r.bone, rng.below(count(r.bone))));
                else passed(r, s);
                continue;
            }
            auto mv = moves[rng.below((uint8_t)moves.size())];
            uint8_t pts = place(r, s, (uint8_t)mv.first, (uint8_t)mv.second);
            nv.put(lo((uint8_t)mv.first), hi((uint8_t)mv.first), mv.second);
            CHECK_EQ(endSum(r), nv.sum());
            CHECK_EQ(pts, nv.fives && nv.sum() % 5 == 0 ? nv.sum() : 0);
            CHECK_EQ(count(r.hand[0]) + count(r.hand[1]) + count(r.bone) + r.n, 28);
            Round c = r;
            CHECK(rebuild(c));
            CHECK(!memcmp(&c, &r, sizeof r));
        }
        CHECK(over(r, why));
    }
}

// ---------------------------------------------------------------------------
// The layout: every tile on the felt, none over another, each touching the
// one it was played on.
// ---------------------------------------------------------------------------
static long laid, forced;

static void printLayout() {
    static char grid[layout::UH][layout::UW + 1];
    for (int y = 0; y < layout::UH; y++) { memset(grid[y], '.', layout::UW); grid[y][layout::UW] = 0; }
    for (int i = 0; i < layout::n; i++) {
        const layout::Placed &p = layout::at[i];
        for (int y = p.y; y < p.y + p.h() && y < layout::UH; y++)
            for (int x = p.x; x < p.x + p.w() && x < layout::UW; x++)
                grid[y][x] = grid[y][x] == '.' ? (char)((i < 10 ? '0' : 'a' - 10) + i) : '#';
    }
    for (int y = 0; y < layout::UH; y++) puts(grid[y]);
    for (int i = 0; i < layout::n; i++) printf("%d:%d/%d ", i, match::round.play[i] & 31, match::round.play[i] >> 5);
    puts("");
}

static void checkLayout() {
    static uint8_t grid[layout::UH][layout::UW];
    memset(grid, 0, sizeof grid);
    for (int i = 0; i < layout::n; i++) {
        const layout::Placed &p = layout::at[i];
        CHECK(p.x + p.w() <= layout::UW && p.y + p.h() <= layout::UH);
        for (int y = p.y; y < p.y + p.h() && y < layout::UH; y++)
            for (int x = p.x; x < p.x + p.w() && x < layout::UW; x++) {
                CHECK(!grid[y][x]);
                grid[y][x] = 1;
            }
    }
}

// ---------------------------------------------------------------------------
// Whole matches through match:: (the stage never busy), humans played at
// random or by the hint.
// ---------------------------------------------------------------------------
struct Outcome { int winner, rounds, s0, s1; unsigned long events; };

static Outcome playMatch(const match::Setup &s, bool resume, int saveAt, bool smart, bool layoutToo) {
    Outcome o = {};
    if (!resume) match::start(s);
    Rng pick;
    pick.seed(s.seed, 77);
    long guard = 0;
    int shown = 0;
    while (!match::matchOver() && guard++ < 200000) {
        match::Event e;
        while (match::popEvent(e)) {
            o.events = o.events * 31 + e.type + 7 * e.a + 13 * e.b + 17 * e.c + 19 * e.d;
            if (e.type == match::EV_START) {
                shown = e.a ? match::round.n : 0;
                if (layoutToo) layout::rebuild(match::round, (uint8_t)shown);
            }
            if (e.type == match::EV_PLAY) {
                if (layoutToo) {
                    layout::Placed p, q;
                    bool ok = layout::plan(e.b, e.c, p);
                    CHECK_EQ(layout::n, shown);
                    bool ok2 = layout::add(e.b, e.c);
                    q = layout::at[layout::n - 1];
                    CHECK(ok == ok2 && p.x == q.x && p.y == q.y && p.v == q.v);
                    laid++;
                    if (!ok) { forced++; if (forced < 4) printLayout(); }
                    else checkLayout();
                }
                shown++;
                CHECK(e.d % 5 == 0);
                CHECK(s.game == FIVES || e.d == 0);
            }
            if (e.type == match::EV_ROUND) {
                o.rounds++;
                CHECK(e.a <= 2);
                if (e.b == DOMINO) CHECK(!match::round.hand[e.a]);
                else CHECK(!match::round.bone && !canPlay(match::round, 0) && !canPlay(match::round, 1));
            }
        }
        if (saveAt >= 0 && guard == saveAt && match::active()) return o;
        if (match::between()) { match::nextRound(); continue; }
        if (match::humanToDraw()) { match::draw(); continue; }
        if (match::humanToPlay()) {
            ai::Move m;
            if (smart) { CHECK(match::hint(m)); }
            else {
                // Any tile, any of the arms on offer.
                uint8_t s2 = match::round.turn, arms[ARMS], k = 0;
                uint8_t start = pick.below(28);
                m.tile = NONE;
                for (uint8_t i = 0; i < TILES && m.tile == NONE; i++) {
                    uint8_t t = (uint8_t)((start + i) % TILES);
                    if (((match::round.hand[s2] >> t) & 1) && (k = match::options(t, arms))) { m.tile = t; m.arm = arms[pick.below(k)]; }
                }
                CHECK(m.tile != NONE);
            }
            CHECK(match::play(m.tile, m.arm));
            continue;
        }
        match::update(false);
    }
    CHECK(match::matchOver());
    o.s0 = match::score[0]; o.s1 = match::score[1];
    o.winner = o.s0 > o.s1 ? 0 : 1;
    CHECK(o.s0 != o.s1 && (o.s0 >= match::target() || o.s1 >= match::target()));
    return o;
}

static void testMatches(int games) {
    for (int g = 0; g < games; g++) {
        match::Setup s = {(uint8_t)(g & 1 ? match::VS_CPU : match::TWO_PLAYER), (uint8_t)(g % 3),
                          (uint8_t)(g & 2 ? FIVES : DRAW), (uint8_t)(g & 2 ? 20 : 10), (uint32_t)(1000 + g)};
        Outcome a = playMatch(s, false, -1, g & 4, true);
        // The same seed plays the same match.
        Outcome b = playMatch(s, false, -1, g & 4, false);
        CHECK_EQ(a.events, b.events);
        CHECK_EQ(a.s0, b.s0);
    }
}

static void testSave(int games) {
    for (int g = 0; g < games; g++) {
        match::Setup s = {match::VS_CPU, 2, (uint8_t)(g & 1), 20, (uint32_t)(5000 + g)};
        playMatch(s, false, 20 + g * 7 % 200, true, false);
        if (match::matchOver()) continue;
        if (match::between()) match::nextRound();
        match::Record rec;
        match::save(rec);
        // On from here; then again from the record: the same match.
        match::Setup t = s;
        t.seed += 99;                       // (the human's picks: the same both times)
        Outcome a = playMatch(t, true, -1, true, false);
        match::Setup junk = {match::TWO_PLAYER, 0, DRAW, 10, 1};
        match::start(junk);
        CHECK(match::load(rec));
        match::Event e;
        CHECK(match::peekEvent(e) && e.type == match::EV_START && e.a == 1);
        Outcome b = playMatch(t, true, -1, true, false);
        CHECK_EQ(a.s0, b.s0);
        CHECK_EQ(a.s1, b.s1);
        CHECK_EQ(a.rounds, b.rounds);
        // A damaged record is refused.
        match::Record bad = rec;
        bad.round.hand[0] ^= 1u << (g % 28);
        CHECK(!match::load(bad));
        bad = rec;
        bad.round.n = 29;
        CHECK(!match::load(bad));
    }
}

// The CPU's levels against each other, by the rules alone: matches to 100.
static int duel(uint8_t la, uint8_t lb, uint8_t game, int games, uint32_t seed) {
    int wonA = 0;
    Rng rng, coin;
    rng.seed(seed, 3);
    coin.seed(seed, 4);
    for (int g = 0; g < games; g++) {
        int score[2] = {0, 0}, target = game == FIVES ? 100 : 50;
        uint8_t lead = (uint8_t)(g & 1);
        bool first = true;
        while ((score[0] < target && score[1] < target) || score[0] == score[1]) {
            uint8_t order[TILES];
            shuffle(order, rng);
            Round r;
            deal(r, game, order, lead);
            uint8_t why, t;
            if (first) { uint8_t s = opener(r, t); score[s] += place(r, s, t, 0); first = false; }
            while (!over(r, why)) {
                uint8_t s = r.turn;
                ai::Move m;
                if (ai::choose(r, s, s ? lb : la, coin, m)) {
                    CHECK((armsFor(r, m.tile) >> m.arm) & 1);
                    score[s] += place(r, s, m.tile, m.arm);
                } else if (r.bone) drew(r, s, nth(r.bone, rng.below(count(r.bone))));
                else passed(r, s);
            }
            uint8_t w, pts = settle(r, why, w);
            if (w < 2) { score[w] += pts; lead = w; }
        }
        wonA += score[0] > score[1];
    }
    return wonA;
}

static void testCpu(int games) {
    static const char *const NAME[3] = {"ROOKIE", "REGULAR", "SHARK"};
    for (uint8_t game = 0; game < 2; game++)
        for (uint8_t a = 1; a < 3; a++) {
            // Each pair twice, seats swapped, so the lead in round one evens out.
            int won = duel(a, (uint8_t)(a - 1), game, games, 11) + (games - duel((uint8_t)(a - 1), a, game, games, 12));
            printf("  %-7s beats %-7s in %4.1f%% of %d matches (%s)\n", NAME[a], NAME[a - 1],
                   100.0 * won / (2 * games), 2 * games, game ? "ALL FIVES" : "DRAW");
            CHECK(won > games);
        }
}

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "--quick");
    testTiles();
    testScoring();
    testAgainstNaive(quick ? 2000 : 40000);
    printf("rules: %ld checks\n", checks);
    testMatches(quick ? 400 : 20000);
    printf("matches: %ld tiles laid out, %ld with no room (%.4f%%)\n", laid, forced, laid ? 100.0 * forced / laid : 0.0);
    CHECK_EQ(forced, 0);
    testSave(quick ? 50 : 400);
    testCpu(quick ? 150 : 1000);
    printf("%ld checks, %ld failures\n", checks, failures);
    return failures ? 1 : 0;
}
