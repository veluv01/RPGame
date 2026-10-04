// Host tests for the checkers engine and the match flow.
//
//     rpgame test
//
// The engine is #included (so its internals can be counted), and checked
// against published perft numbers and against a second, naive move
// generator written here for every combination of the house rules.
#define ENG_CHECKS 1
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../../Engine.cpp"
#include "../../Match.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long x_ = (long long)(a), y_ = (long long)(b); \
    if (x_ != y_) { fails++; printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, x_, y_); } } while (0)

static uint32_t trng = 12345;
static uint32_t rnd() { trng ^= trng << 13; trng ^= trng >> 17; trng ^= trng << 5; return trng; }

// ---------------------------------------------------------------------------
// The engine's own perft: complete moves, a multiple jump counted by path.
// ---------------------------------------------------------------------------
namespace eng {
static uint64_t perft(int d, uint8_t base) {
    if (!d) return 1;
    IStep *m = pool + base;
    uint8_t n = gen(m, (uint8_t)(ENG_POOL - base), false);
    uint64_t r = 0;
    for (uint8_t i = 0; i < n; i++) {
        Undo u;
        bool done = apply(m[i], u);
        r += perft(done ? d - 1 : d, (uint8_t)(base + n));
        unapply(m[i], u);
    }
    return r;
}
}  // namespace eng

// ---------------------------------------------------------------------------
// A second generator: an 8x8 array and whole moves, nothing shared with
// the engine but the meaning of the rules.
// ---------------------------------------------------------------------------
namespace ref {
struct B { uint8_t s[64]; bool black; };
static bool on(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }
static const int DR[4] = {1, 1, -1, -1}, DF[4] = {-1, 1, -1, 1};

struct Jump { int cap, land; };
static std::vector<Jump> jumps(const B &b, int sq, uint8_t rules) {
    std::vector<Jump> out;
    uint8_t p = b.s[sq];
    bool king = (p & 7) == eng::KING, black = (p & eng::BLACK) != 0;
    for (int d = 0; d < 4; d++) {
        bool forward = black ? DR[d] < 0 : DR[d] > 0;
        if (!king && !forward && !(rules & eng::R_BACKJUMP)) continue;
        int r = (sq >> 3) + DR[d], f = (sq & 7) + DF[d];
        if (king && (rules & eng::R_FLYING)) while (on(r, f) && !b.s[r * 8 + f]) { r += DR[d]; f += DF[d]; }
        if (!on(r, f)) continue;
        uint8_t v = b.s[r * 8 + f];
        if (!v || ((v & eng::BLACK) != 0) == black) continue;
        int cap = r * 8 + f;
        r += DR[d]; f += DF[d];
        while (on(r, f) && !b.s[r * 8 + f]) {
            out.push_back({cap, r * 8 + f});
            if (!(king && (rules & eng::R_FLYING))) break;
            r += DR[d]; f += DF[d];
        }
    }
    return out;
}

static void chain(const B &b, int sq, uint8_t rules, std::vector<B> &out) {
    for (const Jump &j : jumps(b, sq, rules)) {
        B nb = b;
        uint8_t p = nb.s[sq];
        nb.s[sq] = 0; nb.s[j.cap] = 0;
        bool crown = (p & 7) == eng::MAN && ((p & eng::BLACK) ? (j.land >> 3) == 0 : (j.land >> 3) == 7);
        if (crown) p = (uint8_t)(eng::KING | (p & eng::BLACK));
        nb.s[j.land] = p;
        if (!crown && !jumps(nb, j.land, rules).empty()) chain(nb, j.land, rules, out);
        else { nb.black = !nb.black; out.push_back(nb); }
    }
}

static std::vector<B> moves(const B &b, uint8_t rules) {
    std::vector<B> out;
    for (int sq = 0; sq < 64; sq++)
        if (b.s[sq] && ((b.s[sq] & eng::BLACK) != 0) == b.black) chain(b, sq, rules, out);
    if (!out.empty() && (rules & eng::R_FORCED)) return out;
    for (int sq = 0; sq < 64; sq++) {
        uint8_t p = b.s[sq];
        if (!p || ((p & eng::BLACK) != 0) != b.black) continue;
        bool king = (p & 7) == eng::KING;
        for (int d = 0; d < 4; d++) {
            bool forward = b.black ? DR[d] < 0 : DR[d] > 0;
            if (!king && !forward) continue;
            int r = (sq >> 3) + DR[d], f = (sq & 7) + DF[d];
            while (on(r, f) && !b.s[r * 8 + f]) {
                B nb = b;
                uint8_t q = p;
                if (!king && (b.black ? r == 0 : r == 7)) q = (uint8_t)(eng::KING | (p & eng::BLACK));
                nb.s[sq] = 0; nb.s[r * 8 + f] = q;
                nb.black = !nb.black;
                out.push_back(nb);
                if (!(king && (rules & eng::R_FLYING))) break;
                r += DR[d]; f += DF[d];
            }
        }
    }
    return out;
}

static uint64_t perft(const B &b, int d, uint8_t rules) {
    if (!d) return 1;
    uint64_t r = 0;
    for (const B &nb : moves(b, rules)) r += perft(nb, d - 1, rules);
    return r;
}

static B fromEngine() {
    B b;
    for (int s = 0; s < 64; s++) b.s[s] = eng::pieceAt((uint8_t)s);
    b.black = eng::blackToMove();
    return b;
}
}  // namespace ref

// One random complete move in the engine. False if there is none.
static bool randomMove() {
    for (;;) {
        uint8_t n = eng::stepCount();
        if (!n) return false;
        if (eng::play((uint8_t)(rnd() % n))) return true;
    }
}

static void testPerft() {
    static const uint64_t WANT[8] = {1, 7, 49, 302, 1469, 7361, 36768, 179740};
    eng::newGame(eng::R_FORCED);
    for (int d = 1; d <= 7; d++) CHECK_EQ(eng::perft(d, 0), WANT[d]);
    CHECK_EQ(ref::perft(ref::fromEngine(), 6, eng::R_FORCED), WANT[6]);
}

static void testAgainstReference() {
    int positions = 0;
    for (uint8_t rules = 0; rules <= eng::R_ALL; rules++) {
        for (int game = 0; game < 30; game++) {
            eng::newGame(rules);
            for (int mv = 0; mv < 90; mv++) {
                if (!randomMove()) break;
                if (eng::status() != eng::NORMAL) break;
                if (mv % 6 != 5) continue;
                uint64_t a = eng::perft(3, 0), b = ref::perft(ref::fromEngine(), 3, rules);
                CHECK_EQ(a, b);
                if (a != b) return;
                positions++;
            }
        }
    }
    printf("reference generator: %d positions agree\n", positions);
}

static uint8_t sq(const char *name) { return (uint8_t)((name[1] - '1') * 8 + (name[0] - 'a')); }

static void testRules() {
    eng::Step s[64];
    // A jump is compulsory under R_FORCED, a choice without.
    // Cells: rank 1 a1 c1 e1 g1, rank 2 b2 d2 f2 h2, rank 3 a3 c3 e3 g3, ...
    // White man c3, black man d4, e5 empty: c3xe5.
    {
        char c[33];
        memset(c, '.', 32); c[32] = 0;
        c[9] = 'w';         // c3
        c[13] = 'b';        // d4
        c[0] = 'w';         // a1 (a quiet move to offer)
        eng::setup(c, false, eng::R_FORCED);
        CHECK_EQ(eng::pieceAt(sq("c3")), eng::MAN);
        CHECK_EQ(eng::pieceAt(sq("d4")), eng::MAN | eng::BLACK);
        uint8_t n = eng::steps(s, 64);
        CHECK_EQ(n, 1);
        CHECK_EQ(s[0].from, sq("c3")); CHECK_EQ(s[0].to, sq("e5")); CHECK_EQ(s[0].cap, sq("d4"));
        CHECK(eng::canJump());
        CHECK_EQ(eng::stepsFrom(sq("a1"), s, 64), 0);
        eng::setup(c, false, 0);
        CHECK_EQ(eng::steps(s, 64), 3);             // the jump, c3-b4, a1-b2
        CHECK(eng::play(0));
        CHECK_EQ(eng::pieceAt(sq("d4")), eng::EMPTY);
        CHECK_EQ(eng::pieceAt(sq("e5")), eng::MAN);
        CHECK_EQ(eng::status(), eng::LOST_NO_PIECES);
    }
    // A double jump: c3xe5xg7, the turn staying with the piece between.
    {
        char c[33];
        memset(c, '.', 32); c[32] = 0;
        c[9] = 'w'; c[13] = 'b'; c[22] = 'b';       // c3, d4, f6
        c[31] = 'b';                                // h8: Black still has a piece after
        eng::setup(c, false, eng::R_FORCED);
        CHECK_EQ(eng::pieceAt(sq("f6")), eng::MAN | eng::BLACK);
        CHECK(!eng::play((uint8_t)eng::indexOf(sq("c3"), sq("e5"))));
        CHECK_EQ(eng::chainSq(), sq("e5"));
        CHECK(!eng::blackToMove());
        CHECK_EQ(eng::status(), eng::NORMAL);
        CHECK_EQ(eng::steps(s, 64), 1);
        CHECK_EQ(s[0].to, sq("g7"));
        CHECK(eng::play(0));
        CHECK_EQ(eng::chainSq(), eng::NONE);
        CHECK(eng::blackToMove());
        CHECK_EQ(eng::count(true), 1);
        CHECK_EQ(eng::ply(), 21);
    }
    // Crowning ends the move, even with a jump on from the far row.
    {
        char c[33];
        memset(c, '.', 32); c[32] = 0;
        // White d6 takes e7 and lands on f8, crowned; g7 could be taken next (to h6).
        auto at = [&](const char *n, char ch) { uint8_t q = sq(n); c[(q >> 3) * 4 + ((q & 7) >> 1)] = ch; };
        at("d6", 'w'); at("e7", 'b'); at("g7", 'b'); at("a1", 'b');
        eng::setup(c, false, eng::R_ALL);
        CHECK(eng::play((uint8_t)eng::indexOf(sq("d6"), sq("f8"))));
        CHECK_EQ(eng::pieceAt(sq("f8")), eng::KING);
        CHECK(eng::blackToMove());
        // Without the crowning (a king already), it jumps on.
        memset(c, '.', 32);
        at("d6", 'W'); at("e7", 'b'); at("g7", 'b'); at("a1", 'b');
        eng::setup(c, false, eng::R_FORCED);
        CHECK(!eng::play((uint8_t)eng::indexOf(sq("d6"), sq("f8"))));
        CHECK_EQ(eng::chainSq(), sq("f8"));
        CHECK(eng::play((uint8_t)eng::indexOf(sq("f8"), sq("h6"))));
        CHECK_EQ(eng::count(true), 1);

        // Men jump backwards only with R_BACKJUMP.
        memset(c, '.', 32);
        at("e5", 'w'); at("d4", 'b'); at("h8", 'b');
        eng::setup(c, false, eng::R_FORCED);
        CHECK(!eng::canJump());
        eng::setup(c, false, eng::R_FORCED | eng::R_BACKJUMP);
        CHECK(eng::canJump());
        CHECK_EQ(eng::indexOf(sq("e5"), sq("c3")), 0);

        // A flying king: slides any distance, and lands anywhere beyond its prey.
        memset(c, '.', 32);
        at("a1", 'W'); at("d4", 'b'); at("h2", 'b');
        eng::setup(c, false, eng::R_FORCED);
        CHECK(!eng::canJump());
        CHECK_EQ(eng::steps(s, 64), 1);             // a1-b2
        eng::setup(c, false, eng::R_FORCED | eng::R_FLYING);
        CHECK_EQ(eng::steps(s, 64), 4);             // a1xd4 landing e5, f6, g7 or h8
        for (int i = 0; i < 4; i++) CHECK_EQ(s[i].cap, sq("d4"));
        eng::setup(c, false, eng::R_FLYING);
        CHECK_EQ(eng::steps(s, 64), 6);             // ... or stopping short on b2, c3

        // Blocked: pieces, but none can move.
        memset(c, '.', 32);
        at("a7", 'w'); at("b8", 'b');
        eng::setup(c, false, eng::R_FORCED);
        CHECK_EQ(eng::status(), eng::LOST_BLOCKED);
        eng::setup(c, true, eng::R_FORCED);
        CHECK_EQ(eng::status(), eng::NORMAL);

        // The same position a third time.
        memset(c, '.', 32);
        at("a1", 'W'); at("h8", 'B');
        eng::setup(c, false, eng::R_FORCED);
        static const char *const LOOP[4][2] = {{"a1", "b2"}, {"h8", "g7"}, {"b2", "a1"}, {"g7", "h8"}};
        for (int k = 0; k < 8; k++) {
            CHECK_EQ(eng::status(), eng::NORMAL);
            CHECK(eng::play((uint8_t)eng::indexOf(sq(LOOP[k & 3][0]), sq(LOOP[k & 3][1]))));
        }
        CHECK_EQ(eng::status(), eng::DRAW_REPETITION);
    }
    // Snapshots.
    {
        eng::newGame(eng::R_FORCED | eng::R_FLYING);
        for (int i = 0; i < 37; i++) randomMove();
        ref::B before = ref::fromEngine();
        eng::Snap sn;
        eng::snapshot(sn);
        uint16_t ply = eng::ply();
        eng::newGame(0);
        eng::restore(sn, eng::R_FORCED | eng::R_FLYING);
        ref::B after = ref::fromEngine();
        CHECK(!memcmp(before.s, after.s, 64));
        CHECK_EQ(before.black, after.black);
        CHECK_EQ(eng::ply(), ply);
        CHECK_EQ(eng::rules(), eng::R_FORCED | eng::R_FLYING);
    }
    // steps / stepsFrom / indexOf / stepAt agree.
    for (uint8_t rules = 0; rules <= eng::R_ALL; rules++) {
        eng::newGame(rules);
        for (int mv = 0; mv < 60 && eng::status() == eng::NORMAL; mv++) {
            uint8_t n = eng::steps(s, 64);
            CHECK_EQ(n, eng::stepCount());
            uint8_t fromTotal = 0;
            for (uint8_t q = 0; q < 64; q++) { eng::Step t[32]; fromTotal = (uint8_t)(fromTotal + eng::stepsFrom(q, t, 32)); }
            CHECK_EQ(fromTotal, n);
            for (uint8_t i = 0; i < n; i++) {
                eng::Step t;
                CHECK(eng::stepAt(i, t));
                CHECK(t.from == s[i].from && t.to == s[i].to && t.cap == s[i].cap);
                CHECK_EQ(eng::indexOf(s[i].from, s[i].to), i);
            }
            if (!randomMove()) break;
        }
    }
}

// Both ways a game without progress ends are reached by random king shuffles.
static void testDraws() {
    int by40 = 0, byRep = 0;
    for (int g = 0; g < 300; g++) {
        char c[33];
        memset(c, '.', 32); c[32] = 0;
        c[0] = 'W'; c[2] = 'W'; c[29] = 'B'; c[31] = 'B';
        eng::setup(c, false, eng::R_FORCED);
        int moves = 0;
        while (eng::status() == eng::NORMAL && moves < 400) {
            // Never offer a jump: keep to moves after which the mover cannot be taken.
            uint8_t n = eng::stepCount(), tried = 0;
            bool played = false;
            while (tried++ < 30 && !played) {
                uint8_t i = (uint8_t)(rnd() % n);
                eng::Undo u;
                eng::apply(eng::pool[i], u);
                bool hangs = eng::gen(eng::pool + 100, 50, true) != 0;
                eng::unapply(eng::pool[i], u);
                eng::stepCount();
                if (hangs) continue;
                eng::play(i);
                played = true;
            }
            if (!played) break;
            moves++;
        }
        if (eng::status() == eng::DRAW_40) { by40++; CHECK_EQ(moves, 80); }
        if (eng::status() == eng::DRAW_REPETITION) { byRep++; CHECK(moves < 80); }
    }
    printf("draws: %d by forty moves, %d by repetition\n", by40, byRep);
    CHECK(by40 > 0);
    CHECK(byRep > 0);
}

// ---------------------------------------------------------------------------
// The match: random games through the same calls the pad makes, with undo
// and save/load on the way.
// ---------------------------------------------------------------------------
static bool sameAsEngine() {
    for (uint8_t s = 0; s < 64; s++) if (match::board[s] != eng::pieceAt(s)) return false;
    return true;
}

static bool humanRandomHop() {
    uint8_t from[64], to[64], n = 0;
    for (uint8_t s = 0; s < 64; s++) {
        uint8_t t[16], c[16], k = match::movesFrom(s, t, c);
        for (uint8_t i = 0; i < k && n < 64; i++) { from[n] = s; to[n++] = t[i]; }
    }
    if (!n) return false;
    uint8_t i = (uint8_t)(rnd() % n);
    return match::play(from[i], to[i]);
}

static void testMatchFuzz() {
    int games = 0, hops = 0, undos = 0, loads = 0, chains = 0, crowns = 0, finals = 0;
    int ends[5] = {0, 0, 0, 0, 0};
    for (uint8_t rules = 0; rules <= eng::R_ALL; rules++) {
        for (int g = 0; g < 250; g++) {
            match::Setup st = {match::TWO_PLAYER, 0, 0, rules, 1};
            match::start(st);
            int guard = 0, onBoard = 24;
            bool last = false;
            while ((match::active() || !last) && guard++ < 20000) {
                last = !match::active();
                match::update(false);
                match::Event e;
                while (match::popEvent(e)) {
                    if (e.type == match::EV_START) { onBoard = eng::count(false) + eng::count(true); }
                    if (e.type == match::EV_HOP) {
                        hops++;
                        if (e.captured) { onBoard--; CHECK(e.hop >= 1); if (e.hop > 1) chains++; }
                        else CHECK_EQ(e.hop, 0);
                        if (e.flags & match::H_CROWN) { crowns++; CHECK(e.flags & match::H_LAST); CHECK_EQ(e.piece & 7, eng::MAN); }
                        if (e.flags & match::H_FINAL) finals++;
                    }
                }
                if (!match::humanToMove()) continue;
                CHECK(sameAsEngine());
                uint32_t r = rnd() % 100;
                if (r < 4 && match::canUndo()) {
                    CHECK(match::undo());
                    undos++;
                    continue;
                }
                if (r < 8) {
                    // Save, wreck the state, load: the same position, mid-jump or not.
                    match::Record rec;
                    match::save(rec);
                    uint8_t was[64];
                    memcpy(was, match::board, 64);
                    bool black = match::blackToMove();
                    uint8_t ch = match::chainSq();
                    match::Setup other = {match::VS_CPU, 1, 2, (uint8_t)(rules ^ 7), 9};
                    match::start(other);
                    CHECK(match::load(rec));
                    CHECK(!memcmp(was, match::board, 64));
                    CHECK_EQ(match::blackToMove(), black);
                    CHECK_EQ(match::chainSq(), ch);
                    CHECK_EQ(match::setup.rules, rules);
                    loads++;
                    continue;
                }
                CHECK(humanRandomHop());
            }
            CHECK(!match::active());
            CHECK(onBoard == eng::count(false) + eng::count(true) || undos);
            ends[match::result]++;
            games++;
        }
    }
    printf("match fuzz: %d games, %d hops (%d in multiple jumps, %d crownings, %d winning), %d undos, %d loads\n",
           games, hops, chains, crowns, finals, undos, loads);
    printf("  ends: white %d, black %d, forty moves %d, repetition %d\n", ends[1], ends[2], ends[3], ends[4]);
    CHECK(chains > 100);
    CHECK(crowns > 100);
    CHECK_EQ(finals, ends[1] + ends[2]);
}

// A long game overflows the step history and is folded into a snapshot;
// undo and saves still work across it.
static void testLongGame() {
    match::Setup st = {match::TWO_PLAYER, 0, 0, 0, 1};      // jumps optional: games go on longer
    int longest = 0, saves = 0;
    for (int g = 0; g < 300; g++) {
        match::start(st);
        int hops = 0;
        while (match::active() && hops < 3000) {
            match::update(false);
            match::Event e;
            while (match::popEvent(e)) {}
            if (!match::humanToMove()) continue;
            if (!humanRandomHop()) break;
            hops++;
            if (hops % 37 == 0 && match::active()) {
                match::Record rec;
                match::save(rec);
                CHECK(rec.n <= sizeof rec.m);
                uint8_t was[64];
                memcpy(was, match::board, 64);
                match::start(st);
                CHECK(match::load(rec));
                CHECK(!memcmp(was, match::board, 64));
                CHECK(sameAsEngine());
                saves += hops > 170;
            }
            if (hops % 41 == 0 && match::canUndo()) CHECK(match::undo());
        }
        if (hops > longest) longest = hops;
    }
    printf("long games: up to %d hops, %d saves past the history's length\n", longest, saves);
    CHECK(longest > 200);
    CHECK(saves > 0);
}

// ---------------------------------------------------------------------------
// The CPU
// ---------------------------------------------------------------------------
static int polls;
static void countPoll() { polls++; }
static void abortPoll() { if (++polls == 3) eng::abort(); }

static void testCpu() {
    // It takes the last piece when it can.
    char c[33];
    memset(c, '.', 32); c[32] = 0;
    auto at = [&](const char *n, char ch) { uint8_t q = sq(n); c[(q >> 3) * 4 + ((q & 7) >> 1)] = ch; };
    at("c3", 'W'); at("d4", 'b'); at("a1", 'w'); at("g1", 'w');
    eng::setup(c, false, 0);
    eng::Level house = match::LEVEL[2];
    int16_t i = eng::think(house);
    eng::Step s;
    CHECK(i >= 0 && eng::stepAt((uint8_t)i, s));
    CHECK_EQ(s.cap, sq("d4"));
    CHECK(eng::lastScore() > 15000);

    // Budget, poll hook, abort.
    eng::newGame(eng::R_FORCED);
    for (int k = 0; k < 6 || eng::canJump() || eng::stepCount() < 4; k++) randomMove();
    polls = 0;
    eng::pollHook = countPoll;
    eng::Level lv = {4000, 0};
    CHECK(eng::think(lv) >= 0);
    CHECK(eng::nodes() <= 4000);
    CHECK(eng::nodes() > 3000);
    CHECK_EQ(polls, (int)(eng::nodes() / eng::POLL_NODES));
    polls = 0;
    eng::pollHook = abortPoll;
    CHECK_EQ(eng::think(lv), -1);
    CHECK(eng::aborted());
    CHECK(eng::nodes() < 200);
    eng::pollHook = nullptr;
    ref::B b0 = ref::fromEngine();
    CHECK(eng::think(lv) >= 0);                  // the board is as it was after a search
    ref::B b1 = ref::fromEngine();
    CHECK(!memcmp(b0.s, b1.s, 64));

    // Whole games against itself under every rule set: always a legal step,
    // every game ends, the same seed plays the same game.
    int results[5] = {0, 0, 0, 0, 0};
    uint32_t nodesTotal = 0, thinks = 0;
    for (uint8_t rules = 0; rules <= eng::R_ALL; rules++) {
        uint32_t sig[2] = {0, 0};
        for (int rep = 0; rep < 2; rep++) {
            match::Setup st = {match::VS_CPU, 0, 0, rules, 77};
            match::start(st);
            trng = 999 + rules;
            int guard = 0;
            while (match::active() && guard++ < 40000) {
                match::update(false);
                match::Event e;
                while (match::popEvent(e))
                    if (e.type == match::EV_HOP) sig[rep] = (sig[rep] ^ (uint32_t)(e.a * 64 + e.b)) * 16777619u;
                if (match::humanToMove()) CHECK(humanRandomHop());
            }
            CHECK(!match::active());
            if (!rep) results[match::result]++;
        }
        CHECK_EQ(sig[0], sig[1]);
    }
    printf("random vs TOURIST: tourist (Black) won %d of 8, lost %d\n", results[2], results[1]);

    // THE HOUSE against the TOURIST, a few games: the stronger level wins.
    int houseWins = 0, touristWins = 0, draws = 0;
    for (int g = 0; g < 6; g++) {
        eng::newGame(eng::R_FORCED);
        eng::seed(1000 + g);
        bool houseBlack = g & 1;
        int moves = 0;
        while (eng::status() == eng::NORMAL && moves < 300) {
            bool houseToMove = eng::blackToMove() == houseBlack;
            eng::Level l = houseToMove ? eng::Level{6000, 0} : match::LEVEL[0];
            int16_t k = eng::think(l);
            CHECK(k >= 0);
            nodesTotal += eng::nodes(); thinks++;
            if (eng::play((uint8_t)k)) moves++;
        }
        eng::Status stt = eng::status();
        if (stt == eng::LOST_NO_PIECES || stt == eng::LOST_BLOCKED) {
            if (eng::blackToMove() == houseBlack) touristWins++; else houseWins++;
        } else draws++;
    }
    printf("6000 nodes vs TOURIST: %d-%d, %d drawn (%u nodes a think on average)\n", houseWins, touristWins, draws,
           thinks ? nodesTotal / thinks : 0);
    CHECK(houseWins >= 5);
}

int main() {
    testPerft();
    testAgainstReference();
    testRules();
    testDraws();
    testMatchFuzz();
    testLongGame();
    testCpu();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
