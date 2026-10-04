// Host tests for the rules, the CPU, the game flow and the dealer's lines.
//
//   rpgame test [--quick]
//
// The bitboard rules are checked against a second, deliberately naive
// implementation written here on a plain 7 x 6 grid.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../Rules.h"
#include "../../Ai.h"
#include "../../Game.h"
#include "../../Taunt.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); failures++; } } while (0)

using namespace c4;

static uint32_t rs = 0x2545F491u;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int rnd(int n) { return (int)(rnd() % (uint32_t)n); }

// ---------------------------------------------------------------------------
// The reference: a grid, g[col][row] = 0 empty, 1 RED, 2 GOLD.
// ---------------------------------------------------------------------------
struct Grid { uint8_t g[7][6]; };

static Grid toGrid(const Board &b) {
    Grid r;
    for (int c = 0; c < 7; c++)
        for (int w = 0; w < 6; w++)
            r.g[c][w] = (b.side[0] & cell(c, w)) ? 1 : (b.side[1] & cell(c, w)) ? 2 : 0;
    return r;
}

static bool refFour(const Grid &g, int v) {
    static const int DC[4] = {0, 1, 1, 1}, DR[4] = {1, 0, 1, -1};
    for (int c = 0; c < 7; c++)
        for (int r = 0; r < 6; r++)
            for (int d = 0; d < 4; d++) {
                int k = 0;
                for (; k < 4; k++) {
                    int cc = c + k * DC[d], rr = r + k * DR[d];
                    if (cc < 0 || cc > 6 || rr < 0 || rr > 5 || g.g[cc][rr] != v) break;
                }
                if (k == 4) return true;
            }
    return false;
}

// Empty cells where a disc of v would make a four.
static Bits refWinning(Grid g, int v) {
    Bits out = 0;
    for (int c = 0; c < 7; c++)
        for (int r = 0; r < 6; r++) {
            if (g.g[c][r]) continue;
            g.g[c][r] = (uint8_t)v;
            if (refFour(g, v)) out |= cell(c, r);
            g.g[c][r] = 0;
        }
    return out;
}

static Board randomBoard(int moves, bool stopAtFour) {
    Board b;
    reset(b);
    for (int i = 0; i < moves; i++) {
        int c = rnd(7);
        if (!canPlay(b, c)) continue;
        play(b, i & 1, c);
        if (stopAtFour && (hasFour(b.side[0]) || hasFour(b.side[1]))) { undo(b, i & 1, c); break; }
    }
    return b;
}

static void testRules(int rounds) {
    CHECK_EQ(count(BOARD), 42);
    CHECK_EQ(count(BOTTOM), 7);
    for (int i = 0; i < rounds; i++) {
        Board b = randomBoard(rnd(43), false);
        Grid g = toGrid(b);
        int n = 0;
        for (int c = 0; c < 7; c++) {
            int h = 0;
            while (h < 6 && g.g[c][h]) h++;
            for (int r = h; r < 6; r++) CHECK(!g.g[c][r]);         // no floating discs
            CHECK_EQ(b.h[c], h);
            n += h;
        }
        CHECK_EQ(b.n, n);
        Bits occ = b.side[0] | b.side[1];
        for (int s = 0; s < 2; s++) {
            CHECK_EQ(hasFour(b.side[s]), refFour(g, s + 1));
            if (!refFour(g, s + 1)) CHECK(winning(b.side[s], occ) == refWinning(g, s + 1));
        }
        Bits can = 0;
        for (int c = 0; c < 7; c++) if (b.h[c] < 6) can |= cell(c, b.h[c]);
        CHECK(playable(b) == can);
        // The four through the last disc of a won game.
        Board w = randomBoard(42, true);
        for (int c = 0; c < 7; c++) {
            for (int s = 0; s < 2; s++) {
                if (!canPlay(w, c)) continue;
                int row = play(w, s, c);
                uint8_t cells[4];
                bool f = fourThrough(w.side[s], c, row, cells);
                CHECK_EQ(f, hasFour(w.side[s]));
                if (f) {
                    Bits m = 0;
                    bool through = false;
                    for (int k = 0; k < 4; k++) {
                        CHECK(cells[k] % 7 < 6 && cells[k] / 7 < 7);
                        m |= (Bits)1 << cells[k];
                        through |= cells[k] == c * 7 + row;
                    }
                    CHECK(through);
                    CHECK_EQ(count(m), 4);
                    CHECK((m & w.side[s]) == m);
                    CHECK(hasFour(m));
                }
                undo(w, s, c);
            }
        }
    }
    printf("rules: %d random boards agree with the grid reference\n", rounds);
}

// ---------------------------------------------------------------------------
// The CPU
// ---------------------------------------------------------------------------
static Board fromMoves(const char *moves, uint8_t &turn) {
    Board b;
    reset(b);
    turn = 0;
    for (; *moves; moves++, turn ^= 1) play(b, turn, *moves - '1');
    return b;
}

static uint8_t think(const Board &b, uint8_t side, uint8_t level, uint32_t seed, uint16_t slice = 65535) {
    Rng r = {seed | 1};
    ai::start(b, side, level, r);
    while (!ai::step(slice)) {}
    return ai::chosen();
}

// A true search to `depth` plies by plain recursion: the value of the
// position for the side to move (win/loss/draw only: 1, -1, 0).
static int solve(Board &b, uint8_t s, int depth) {
    if (immediate(b, s)) return 1;
    if (full(b)) return 0;
    if (depth <= 1) return 0;
    int best = -1;
    for (int c = 0; c < 7; c++) {
        if (!canPlay(b, c)) continue;
        play(b, s, c);
        int v = -solve(b, s ^ 1, depth - 1);
        undo(b, s, c);
        if (v > best) best = v;
        if (best == 1) break;
    }
    return best;
}

static void testAi(bool quick) {
    uint8_t t;
    // A win in one is taken, at every level, whatever else is going on.
    Board b = fromMoves("112233", t);              // RED has 1 2 3 along the bottom, to move
    for (uint8_t lv = 0; lv < ai::LEVELS; lv++)
        for (uint32_t seed = 1; seed < 20; seed++) {
            CHECK_EQ(think(b, RED, lv, seed), 3);
            CHECK(ai::winning(ai::score()));
            CHECK_EQ(ai::movesToWin(ai::score()), 1);
        }
    // ... and a threat is blocked.
    b = fromMoves("1122337", t);                   // GOLD to move: RED threatens column 4
    CHECK_EQ(t, GOLD);
    // (GOLD has 1 2 3 on the second row: no win of its own at column 4 yet.)
    for (uint8_t lv = 0; lv < ai::LEVELS; lv++)
        for (uint32_t seed = 1; seed < 20; seed++) CHECK_EQ(think(b, GOLD, lv, seed), 3);
    // A vertical three.
    b = fromMoves("4141415", t);                   // RED: three in column 4, then 5; GOLD: three in column 1
    CHECK_EQ(think(b, GOLD, ai::ROOKIE, 5), 0);    // its own win before the block

    // Never under the other side's winning cell (unless there is nothing else).
    int under = 0, forcedWins = 0, searches = 0;
    uint32_t most[ai::LEVELS] = {}, total[ai::LEVELS] = {};
    int rounds = quick ? 300 : 3000;
    for (int i = 0; i < rounds; i++) {
        Board p = randomBoard(4 + rnd(30), true);
        if (full(p)) continue;
        uint8_t side = (uint8_t)(p.n & 1);
        for (uint8_t lv = 0; lv < ai::LEVELS; lv++) {
            Board q = p;
            uint8_t col = think(q, side, lv, i + 1);
            int16_t sc = ai::score();
            uint32_t nodes = ai::positions();
            CHECK(canPlay(q, col));
            total[lv] += nodes;
            if (nodes > most[lv]) most[lv] = nodes;
            searches++;
            // Sliced or all at once: the same choice and the same count.
            uint8_t col2 = think(q, side, lv, i + 1, 1 + rnd(40));
            CHECK_EQ(col2, col);
            CHECK_EQ(ai::positions(), nodes);
            CHECK_EQ(ai::score(), sc);
            // An immediate win is taken; an immediate loss is blocked if it can be.
            Bits win = immediate(q, side), lose = immediate(q, side ^ 1);
            if (win) CHECK(win & cell(col, q.h[col]));
            else if (lose) CHECK(lose & cell(col, q.h[col]));
            else {
                // It did not play under a cell the other side wins in, if it had a choice.
                Bits theirs = winning(q.side[side ^ 1], q.side[0] | q.side[1]);
                Bits safe = playable(q) & ~(theirs >> 1);
                if (safe && !(safe & cell(col, q.h[col]))) under++;
            }
            // What it calls a forced win is one: the reference agrees.
            if (lv == ai::BOSS && ai::winning(sc) && !win) {
                int plies = ai::WIN - sc + 1;                  // plies to the four, this move included
                if (plies <= 7) {
                    Board r = q;
                    CHECK_EQ(solve(r, side, plies), 1);
                    forcedWins++;
                    // And its move keeps it.
                    play(r, side, col);
                    CHECK_EQ(solve(r, side ^ 1, plies - 1), -1);
                }
            }
            if (lv == ai::BOSS && ai::losing(sc)) {
                int plies = ai::WIN + sc + 1;                  // the other side's four is that many plies off
                if (plies <= 7) { Board r = q; CHECK_EQ(solve(r, side, plies + 1), -1); }
            }
        }
    }
    CHECK_EQ(under, 0);
    printf("cpu: %d searches; positions visited, mean / most: ROOKIE %u / %u, SHARK %u / %u, BOSS %u / %u\n",
           searches, total[0] * 3 / searches, most[0], total[1] * 3 / searches, most[1], total[2] * 3 / searches, most[2]);
    printf("cpu: %d forced wins checked against a plain search\n", forcedWins);

    // The levels are in order: each beats the one below it over a run of games.
    int games = quick ? 40 : 200;
    for (uint8_t hi = 1; hi < ai::LEVELS; hi++) {
        uint8_t lo = (uint8_t)(hi - 1);
        int won = 0, lost = 0, drawn = 0;
        for (int gme = 0; gme < games; gme++) {
            Board p;
            reset(p);
            uint8_t side = (uint8_t)(gme & 1), strong = RED;   // the stronger is RED; sides take turns to start
            Rng r1 = {(uint32_t)gme * 7919u + 1}, r2 = {(uint32_t)gme * 104729u + 3};
            int result = 0;
            for (;;) {
                uint8_t lv = side == strong ? hi : lo;
                ai::start(p, side, lv, side == strong ? r1 : r2);
                while (!ai::step(65535)) {}
                uint8_t col = ai::chosen();
                play(p, side, col);
                if (hasFour(p.side[side])) { result = side == strong ? 1 : -1; break; }
                if (full(p)) break;
                side ^= 1;
            }
            if (result > 0) won++; else if (result < 0) lost++; else drawn++;
        }
        static const char *const NAME[3] = {"ROOKIE", "SHARK", "BOSS"};
        printf("cpu: %s v %s over %d games: %d won, %d lost, %d drawn\n", NAME[hi], NAME[lo], games, won, lost, drawn);
        CHECK(won > lost * 2);
    }
}

// ---------------------------------------------------------------------------
// The dealer's lines: every one fits the bubble, in the 3x5 font's characters.
// ---------------------------------------------------------------------------
static void testTaunts() {
    static const char OK[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!.-+?:$,/'*()<>=% \n";
    int n = 0;
    for (uint8_t k = 0; k < taunt::KINDS; k++) {
        char buf[taunt::LINE_MAX + 8];
        uint8_t i = 0;
        for (; taunt::line(k, i, buf); i++, n++) {
            CHECK(strlen(buf) < taunt::LINE_MAX);
            CHECK(strlen(buf) > 0);
            int rows = 1, len = 0;
            for (const char *p = buf; *p; p++) {
                if (!strchr(OK, *p)) { printf("FAIL: kind %d line %d has '%c'\n", k, i, *p); failures++; }
                if (*p == '\n') { rows++; len = 0; }
                else if (++len > 12) { printf("FAIL: kind %d line %d: a row over 12 characters\n", k, i); failures++; break; }
            }
            if (rows > 3) { printf("FAIL: kind %d line %d: %d rows\n", k, i, rows); failures++; }
        }
        CHECK(i >= 1 && i <= 8);
        // Every line of a kind is said before any is said again.
        Rng r = {k * 31u + 7};
        taunt::reset();
        char seen[8][taunt::LINE_MAX];
        for (uint8_t j = 0; j < i; j++) {
            taunt::pick(k, r, seen[j], 3);
            for (uint8_t q = 0; q < j; q++) CHECK(strcmp(seen[q], seen[j]) != 0);
        }
    }
    printf("lines: %d, all fit the bubble\n", n);
}

// ---------------------------------------------------------------------------
// Whole games through the game's own calls.
// ---------------------------------------------------------------------------
static void testGames(bool quick) {
    int games = quick ? 60 : 400, said[taunt::KINDS] = {}, over[3] = {};
    for (int gme = 0; gme < games; gme++) {
        game::Setup s = {(uint8_t)(gme % 5 == 4 ? game::TWO_PLAYER : game::VS_CPU), (uint8_t)(gme % 3), (uint8_t)(gme & 1), 1,
                         (uint32_t)gme * 2654435761u + 17};
        game::start(s);
        // The person: a mix of the SHARK's moves and random ones.
        Rng mine = {(uint32_t)gme + 99};
        int forcedIn = 0, dealerMoves = 0, ticks = 0;
        bool sawOver = false, waiting = false;
        uint8_t wait = 0;
        while (!sawOver && ticks++ < 200000) {
            if (game::humanToMove() && !wait) {
                uint8_t col;
                if (rnd(3)) {
                    ai::start(game::board, game::side(), ai::SHARK, mine);
                    while (!ai::step(65535)) {}
                    col = ai::chosen();
                } else {
                    do col = (uint8_t)rnd(7); while (!canPlay(game::board, col));
                }
                CHECK(game::drop(col));
            }
            game::update(wait != 0);
            if (wait) wait--;
            game::Event e;
            while (game::popEvent(e)) {
                switch (e.type) {
                    case game::EV_DROP:
                        CHECK(e.b < 7 && e.c < 6);
                        if (s.mode == game::VS_CPU && e.a == game::DEALER) dealerMoves++;
                        wait = 6;                              // the disc falls
                        break;
                    case game::EV_SAY:
                        CHECK(strlen(game::said) > 0 && strlen(game::said) < taunt::LINE_MAX);
                        CHECK(game::lastKind < taunt::KINDS);
                        said[game::lastKind]++;
                        if (e.b) wait = 10;                    // he talks: the game waits
                        if (game::lastKind == taunt::FORCED) {
                            CHECK(!strchr(game::said, '#'));
                            forcedIn = dealerMoves + ai::movesToWin(ai::score());
                        }
                        // A line about a win in hand is true.
                        if (game::lastKind == taunt::WIN_NOW || game::lastKind == taunt::GIFT || game::lastKind == taunt::UNBLOCKED)
                            CHECK(immediate(game::board, game::DEALER) != 0);
                        if (game::lastKind == taunt::DOUBLE) CHECK(count(immediate(game::board, game::YOU)) >= 2);
                        if (game::lastKind == taunt::TRAP) CHECK(count(immediate(game::board, game::DEALER)) >= 2);
                        if (game::lastKind == taunt::MUST_BLOCK) CHECK(count(immediate(game::board, game::YOU)) == 1);
                        break;
                    case game::EV_OVER:
                        sawOver = true;
                        CHECK(!game::active());
                        CHECK_EQ(e.a, game::winner);
                        over[e.a]++;
                        said[game::lastKind]++;
                        CHECK(strlen(game::said) > 0);
                        if (e.a != NOBODY) {
                            Bits m = 0;
                            for (int k = 0; k < 4; k++) m |= (Bits)1 << game::four[k];
                            CHECK((m & game::board.side[e.a]) == m && hasFour(m));
                        } else CHECK(full(game::board));
                        // He said he would win in so many moves: he did, by then.
                        if (forcedIn) { CHECK_EQ(e.a, game::DEALER); CHECK(dealerMoves <= forcedIn); }
                        break;
                    default: break;
                }
            }
            (void)waiting;
        }
        CHECK(sawOver);
        // Save and reload mid-game: the same position, the same side to move.
        if (gme % 7 == 0) {
            game::Setup s2 = s;
            s2.seed += 5;
            game::start(s2);
            for (int k = 0; k < 6; k++) {
                while (!game::humanToMove() && game::active()) game::update(false);
                if (!game::active()) break;
                uint8_t col;
                do col = (uint8_t)rnd(7); while (!canPlay(game::board, col));
                game::drop(col);
            }
            if (game::active()) {
                game::Record rec;
                game::save(rec);
                Board was = game::board;
                uint8_t turn = game::side();
                game::Setup other = {game::TWO_PLAYER, 0, 0, 0, 1};
                game::start(other);
                CHECK(game::load(rec));
                CHECK(memcmp(&was, &game::board, sizeof was) == 0);
                CHECK_EQ(game::side(), turn);
                CHECK_EQ(game::setup.mode, s2.mode);
                rec.board.h[0]++;
                CHECK(!game::load(rec));                       // a damaged record is refused
                game::start(other);
            }
        }
    }
    printf("games: %d played out (RED %d, GOLD %d, drawn %d)\n", games, over[0], over[1], over[2]);
    printf("lines said by kind:");
    for (uint8_t k = 0; k < taunt::KINDS; k++) printf(" %d", said[k]);
    printf("\n");
    if (!quick) for (uint8_t k = 0; k < taunt::KINDS; k++)
        if (k != taunt::IDLE) CHECK(said[k] > 0);                // every kind of line gets said
}

// --story N: a game against each dealer told move by move, to read how much
// he talks and whether what he says is apt.
static void story(uint32_t seed) {
    static const char *const KIND[taunt::KINDS] = {
        "START", "WIN_NOW", "GIFT", "UNBLOCKED", "FORCED", "MUST_BLOCK", "MISSED", "BLOCKED", "DOUBLE", "LOSING", "TRAP",
        "THREAT", "OPEN_CENTRE", "OPEN_EDGE", "AHEAD", "BEHIND", "BANTER", "IDLE", "DRAWISH", "HE_LOST", "HE_WON", "DRAWN",
        "P2_START", "P2_MISSED", "P2_DOUBLE", "P2_BLOCK", "P2_THREAT", "P2_BANTER", "P2_WON"};
    for (uint8_t lv = 0; lv < 3; lv++) {
        game::Setup s = {game::VS_CPU, lv, (uint8_t)(seed & 1), 0, seed * 7919u + lv};
        game::start(s);
        Rng mine = {seed + 5};
        printf("--- level %d, %s first\n", lv, s.first ? "dealer" : "you");
        bool over = false;
        uint8_t wait = 0;
        while (!over) {
            if (game::humanToMove() && !wait) {
                uint8_t col;
                if (rnd(4)) {
                    ai::start(game::board, game::side(), ai::SHARK, mine);
                    while (!ai::step(65535)) {}
                    col = ai::chosen();
                } else do col = (uint8_t)rnd(7); while (!canPlay(game::board, col));
                game::drop(col);
            }
            game::update(wait != 0);
            if (wait) wait--;
            game::Event e;
            while (game::popEvent(e)) {
                char flat[taunt::LINE_MAX];
                strcpy(flat, game::said);
                for (char *q = flat; *q; q++) if (*q == '\n') *q = ' ';
                if (e.type == game::EV_DROP) { printf("  %s %d\n", e.a ? "    dealer" : "you", e.b + 1); wait = 6; }
                if (e.type == game::EV_SAY) { printf("        [%s] %s\n", KIND[game::lastKind], flat); if (e.b) wait = 10; }
                if (e.type == game::EV_OVER) { printf("  OVER: %s  [%s] %s\n", e.a == 2 ? "draw" : e.a ? "dealer wins" : "you win", KIND[game::lastKind], flat); over = true; }
            }
        }
    }
}

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "--quick");
    if (argc > 2 && !strcmp(argv[1], "--story")) { rs = (uint32_t)atoi(argv[2]) * 2654435761u | 1; story((uint32_t)atoi(argv[2])); return 0; }
    testRules(quick ? 2000 : 20000);
    testAi(quick);
    testTaunts();
    testGames(quick);
    printf(failures ? "%d FAILURES\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
