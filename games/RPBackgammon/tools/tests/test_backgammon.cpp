// Host tests for the rules, the game flow and the CPU.
//
//   rpgame test [--quick]
//
// The rules are checked against a second, deliberately naive implementation
// written here in another representation (one signed array in White's
// coordinates, plain recursion over every order of the dice into a set):
// every play Rules.cpp gives for a roll must be one the reference
// gives, and the other way round.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <array>
#include <set>
#include <vector>
#include "../../Rules.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); failures++; } } while (0)

using namespace bg;

static uint32_t rs = 0x2545F491u;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int rnd(int n) { return (int)(rnd() % (uint32_t)n); }

// ---------------------------------------------------------------------------
// The reference: White's coordinates for both sides. pt[p] > 0: White's
// checkers on p, < 0: Red's. White moves down to 1 and off below it; Red
// moves up to 24 and off above it.
// ---------------------------------------------------------------------------
struct Ref {
    int pt[26];         // 1..24
    int bar[2], off[2];
    bool operator<(const Ref &o) const { return memcmp(this, &o, sizeof *this) < 0; }
    bool operator==(const Ref &o) const { return memcmp(this, &o, sizeof *this) == 0; }
};

static Ref toRef(const Board &b) {
    Ref r;
    memset(&r, 0, sizeof r);
    for (int p = 1; p <= 24; p++) {
        if (b.n[WHITE][p]) r.pt[p] = b.n[WHITE][p];
        if (b.n[RED][25 - p]) r.pt[p] = -b.n[RED][25 - p];
    }
    for (int s = 0; s < 2; s++) { r.bar[s] = b.n[s][BAR]; r.off[s] = b.n[s][OFF]; }
    return r;
}

static int refCount(const Ref &r, int s, int p) { return s == WHITE ? (r.pt[p] > 0 ? r.pt[p] : 0) : (r.pt[p] < 0 ? -r.pt[p] : 0); }

// One checker of side s from src (0 = the bar) by d: false if illegal.
static bool refMove(Ref &r, int s, int src, int d) {
    int dest;
    if (r.bar[s]) {
        if (src != 0) return false;
        dest = s == WHITE ? 25 - d : d;
    } else {
        if (src == 0 || refCount(r, s, src) == 0) return false;
        dest = s == WHITE ? src - d : src + d;
    }
    bool offBoard = dest < 1 || dest > 24;
    if (offBoard) {
        // All fifteen in the home board (or off already)?
        for (int p = 1; p <= 24; p++) {
            bool home = s == WHITE ? p <= 6 : p >= 19;
            if (!home && refCount(r, s, p)) return false;
        }
        bool exact = s == WHITE ? dest == 0 : dest == 25;
        if (!exact) {
            // A bigger die: only from the rearmost checker.
            if (s == WHITE) { for (int p = src + 1; p <= 6; p++) if (refCount(r, s, p)) return false; }
            else            { for (int p = 19; p < src; p++) if (refCount(r, s, p)) return false; }
        }
    } else if (refCount(r, s ^ 1, dest) >= 2) return false;
    // Legal: make it.
    if (src == 0) r.bar[s]--;
    else r.pt[src] += s == WHITE ? -1 : 1;
    if (offBoard) { r.off[s]++; return true; }
    if (refCount(r, s ^ 1, dest) == 1) { r.pt[dest] = 0; r.bar[s ^ 1]++; }
    r.pt[dest] += s == WHITE ? 1 : -1;
    return true;
}

struct RefPlay { Ref end; int used; bool usedHigh; };

static void refWalk(const Ref &r, int s, std::vector<int> dice, int used, bool usedHigh, int hi,
                    std::vector<RefPlay> &out) {
    bool moved = false;
    for (size_t i = 0; i < dice.size(); i++) {
        if (i && dice[i] == dice[i - 1]) continue;
        std::vector<int> rest = dice;
        rest.erase(rest.begin() + (long)i);
        for (int src = 0; src <= 24; src++) {
            Ref n = r;
            if (!refMove(n, s, src, dice[i])) continue;
            moved = true;
            refWalk(n, s, rest, used + 1, usedHigh || dice[i] == hi, hi, out);
        }
    }
    if (!moved) out.push_back({r, used, usedHigh});
}

// The final positions the rules allow for a roll.
static std::set<Ref> refPlays(const Board &b, int s, int d1, int d2, int *needOut = nullptr) {
    std::vector<int> dice = d1 == d2 ? std::vector<int>{d1, d1, d1, d1} : std::vector<int>{d1, d2};
    std::vector<RefPlay> all;
    int hi = d1 > d2 ? d1 : d2;
    refWalk(toRef(b), s, dice, 0, false, hi, all);
    int need = 0;
    bool anyHigh = false;
    for (auto &p : all) if (p.used > need) need = p.used;
    for (auto &p : all) if (p.used == need && p.usedHigh) anyHigh = true;
    std::set<Ref> out;
    for (auto &p : all) {
        if (p.used != need) continue;
        if (d1 != d2 && need == 1 && anyHigh && !p.usedHigh) continue;   // one die only: the higher if possible
        out.insert(p.end);
    }
    if (needOut) *needOut = need;
    return out;
}

// ---------------------------------------------------------------------------
// Positions
// ---------------------------------------------------------------------------
static void clear(Board &b) { memset(&b, 0, sizeof b); }

// Random position: 15 a side, no shared point. kind 0 scattered, 1 both
// bearing off, 2 one side home and the other scattered, 3 from random play.
static void randomPosition(Board &b, int kind) {
    if (kind == 3) {
        reset(b);
        int plies = rnd(120);
        uint8_t s = (uint8_t)rnd(2);
        for (int i = 0; i < plies; i++) {
            uint8_t w;
            if (result(b, w)) { reset(b); }
            Plays g;
            begin(g, b, s, (uint8_t)(1 + rnd(6)), (uint8_t)(1 + rnd(6)));
            int n = 0;
            while (next(g, b)) n++;
            int pick = rnd(n);
            begin(g, b, s, g.a, g.b);
            for (int k = 0; k <= pick; k++) next(g, b);     // leave the picked play on the board
            s ^= 1;
        }
        uint8_t w;
        if (result(b, w)) reset(b);
        return;
    }
    clear(b);
    for (int s = 0; s < 2; s++) {
        bool home = kind == 1 || (kind == 2 && s == 0);
        int left = CHECKERS;
        if (home || rnd(4) == 0) { int o = rnd(home ? 15 : 6); b.n[s][OFF] = (uint8_t)o; left -= o; }
        if (!home && rnd(3) == 0) { int o = 1 + rnd(3); if (o > left) o = left; b.n[s][BAR] = (uint8_t)o; left -= o; }
        int tries = 0;
        while (left) {
            int p = 1 + rnd(home ? 6 : 24);
            if (b.n[s ^ 1][25 - p]) { if (++tries > 2000) break; continue; }
            int k = 1 + rnd(left < 4 ? left : 4);
            b.n[s][p] = (uint8_t)(b.n[s][p] + k);
            left -= k;
        }
        b.n[s][OFF] = (uint8_t)(b.n[s][OFF] + left);        // could not place them: off
    }
    recount(b);
    uint8_t w;
    if (result(b, w)) randomPosition(b, kind);
}

static const char *show(const Board &b) {
    static char buf[200];
    char *p = buf;
    for (int s = 0; s < 2; s++) {
        p += sprintf(p, "%c:", s ? 'R' : 'W');
        for (int i = 25; i >= 0; i--) if (b.n[s][i]) p += sprintf(p, " %d@%d", b.n[s][i], i);
        p += sprintf(p, "  ");
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static long totalPlays, totalDup, totalRolls, maxPlays;

static void crossCheck(Board &b, uint8_t s, uint8_t d1, uint8_t d2) {
    Board before = b;
    int need;
    std::set<Ref> want = refPlays(b, s, d1, d2, &need);
    CHECK_EQ(maxPlayable(b, s, roll2(d1, d2)), need);
    std::set<Ref> got;
    Plays g;
    begin(g, b, s, d1, d2);
    long n = 0, dup = 0;
    while (next(g, b)) {
        n++;
        if (!valid(b)) { printf("FAIL invalid board after a play: %s\n", show(b)); failures++; }
        CHECK_EQ(g.depth, need);
        if (!got.insert(toRef(b)).second) dup++;
    }
    CHECK(memcmp(b.n, before.n, sizeof b.n) == 0);
    if (got != want) {
        printf("FAIL plays differ: side %d roll %d-%d  %s  (got %zu, reference %zu)\n", s, d1, d2, show(before),
               got.size(), want.size());
        failures++;
    }
    if (dup) {
        printf("FAIL %ld repeated plays: side %d roll %d-%d  %s\n", dup, s, d1, d2, show(before));
        failures++;
    }
    totalPlays += n; totalDup += dup; totalRolls++;
    if (n > maxPlays) maxPlays = n;
}

// Every way a player could play the turn a step at a time ends on a
// reference position, and can never get stuck with dice it had to play.
static void closure(Turn &t, Board &b, uint8_t s, std::set<Ref> &ends, bool &stuck, bool &exempt) {
    if (turnDone(t)) { ends.insert(toRef(b)); return; }
    bool any = false;
    const Dice dice = t.dice;                           // (taking a step back reorders t.dice)
    for (uint8_t f = BAR; f; f--)
        for (uint8_t i = 0; i < dice.n; i++) {
            if (i && dice.d[i] == dice.d[i - 1]) continue;
            uint8_t die = dice.d[i];
            if (!stepAllowed(t, b, s, f, die)) continue;
            any = true;
            Board keep = b;
            Turn keepT = t;
            bool lastOff = b.n[s][OFF] == CHECKERS - 1;
            playStep(t, b, s, f, die);
            if (lastOff && t.steps < keepT.steps + keepT.need) exempt = true;   // won without using every die
            closure(t, b, s, ends, stuck, exempt);
            CHECK(takeBack(t, b, s));
            CHECK(memcmp(b.n, keep.n, sizeof b.n) == 0);
            CHECK_EQ(t.need, keepT.need);
            CHECK_EQ(t.dice.n, keepT.dice.n);
            CHECK_EQ(t.steps, keepT.steps);
        }
    if (!any) stuck = true;
}

static void validatorCheck(Board &b, uint8_t s, uint8_t d1, uint8_t d2) {
    std::set<Ref> want = refPlays(b, s, d1, d2);
    Turn t;
    beginTurn(t, b, s, d1, d2);
    std::set<Ref> ends;
    bool stuck = false, exempt = false;
    closure(t, b, s, ends, stuck, exempt);
    if (stuck) { printf("FAIL validator dead end: side %d roll %d-%d  %s\n", s, d1, d2, show(b)); failures++; }
    bool same = ends == want;
    if (!same && exempt) {
        // The last checker borne off with one die where two could be used:
        // extra positions are allowed, as long as each is a win.
        same = true;
        for (auto &e : ends) if (!want.count(e) && e.off[s] != CHECKERS) same = false;
        for (auto &w : want) if (!ends.count(w)) same = false;
    }
    if (!same) {
        printf("FAIL validator: side %d roll %d-%d  %s (%zu ends, reference %zu)\n", s, d1, d2, show(b), ends.size(),
               want.size());
        failures++;
    }
    // Targets: each is a route the validator accepts, to a different place.
    for (uint8_t f = BAR; f; f--) {
        Target tg[4];
        Board keep = b;
        uint8_t n = targets(t, b, s, f, tg);
        CHECK(n <= 4);
        CHECK(memcmp(b.n, keep.n, sizeof b.n) == 0);
        bool single = false;
        for (uint8_t i = 0; i < t.dice.n; i++) single |= stepAllowed(t, b, s, f, t.dice.d[i]);
        CHECK_EQ(n > 0, single);
        for (uint8_t i = 0; i < n; i++) {
            for (uint8_t j = 0; j < i; j++) CHECK(tg[i].to != tg[j].to);
            uint8_t at = f;
            for (uint8_t k = 0; k < tg[i].n; k++) {
                CHECK(stepAllowed(t, b, s, at, tg[i].die[k]));
                bool hit = playStep(t, b, s, at, tg[i].die[k]);
                CHECK_EQ(hit, (tg[i].hits >> k) & 1);
                at = landing(at, tg[i].die[k]);
            }
            CHECK_EQ(at, tg[i].to);
            for (uint8_t k = 0; k < tg[i].n; k++) takeBack(t, b, s);
            CHECK(memcmp(b.n, keep.n, sizeof b.n) == 0);
        }
    }
}

static void put(Board &b, uint8_t s, std::initializer_list<int> pointCount) {
    auto it = pointCount.begin();
    while (it != pointCount.end()) { int p = *it++; int n = *it++; b.n[s][p] = (uint8_t)n; }
    int sum = 0;
    for (int p = 1; p <= BAR; p++) sum += b.n[s][p];
    b.n[s][OFF] = (uint8_t)(CHECKERS - sum);
    recount(b);
}

static int countPlays(Board &b, uint8_t s, uint8_t d1, uint8_t d2) {
    Plays g;
    begin(g, b, s, d1, d2);
    int n = 0;
    while (next(g, b)) n++;
    return n;
}

static void testRules() {
    Board b;
    reset(b);
    CHECK(valid(b));
    CHECK_EQ(pips(b, WHITE), 167);
    CHECK_EQ(pips(b, RED), 167);
    CHECK(contact(b));
    // Opening: 24/18 is open (the other side's 7 point is empty), 24/19 is
    // blocked by its 6 point, 13/8 lands on our own.
    CHECK(canStep(b, WHITE, 24, 6));
    CHECK(!canStep(b, WHITE, 24, 5));
    CHECK(canStep(b, WHITE, 13, 5));
    CHECK(!canStep(b, WHITE, 6, 6));        // no bearing off with checkers outside
    CHECK(!canStep(b, WHITE, 12, 1));       // nothing there

    // A hit sends the blot to the bar, and it must come in before anything else moves.
    clear(b);
    put(b, WHITE, {24, 2, 13, 5, 8, 3, 6, 5});
    put(b, RED, {24, 2, 13, 5, 8, 2, 6, 5, 4, 1});      // Red's blot on its 4 = White's 21
    CHECK(valid(b));
    CHECK(doStep(b, WHITE, 24, 3));                     // 24/21*: hit
    CHECK_EQ(b.n[RED][BAR], 1);
    CHECK_EQ(b.n[RED][4], 0);
    CHECK(valid(b));
    CHECK(!canStep(b, RED, 13, 2));                     // on the bar: nothing else may move
    CHECK(canStep(b, RED, BAR, 2));                     // comes in on its 23 (White's 2)
    CHECK(!canStep(b, RED, BAR, 6));                    // White's 6 point is made
    undoStep(b, WHITE, 24, 3, true);
    CHECK_EQ(b.n[RED][4], 1);
    CHECK_EQ(b.n[RED][BAR], 0);
    CHECK(valid(b));

    // Dancing: every entry point made -> no move at all.
    clear(b);
    put(b, WHITE, {BAR, 1, 13, 4, 8, 4, 6, 6});
    put(b, RED, {1, 2, 2, 2, 3, 2, 4, 2, 5, 2, 6, 5});  // Red owns its whole home board
    CHECK(valid(b));
    CHECK_EQ(maxPlayable(b, WHITE, roll2(6, 3)), 0);
    CHECK_EQ(countPlays(b, WHITE, 6, 3), 1);            // the pass

    // Only one die playable. White on 10 and 4; Red holds White's 9 and 3,
    // so no 1 can be played, before or after the 6 (10/4).
    clear(b);
    put(b, WHITE, {10, 1, 4, 1});
    put(b, RED, {16, 2, 22, 2, 6, 11});                 // Red's 16 = White's 9, Red's 22 = White's 3
    CHECK(valid(b));
    CHECK(!canStep(b, WHITE, 10, 1));
    CHECK(!canStep(b, WHITE, 4, 1));
    CHECK(canStep(b, WHITE, 10, 6));
    {
        Turn t;
        beginTurn(t, b, WHITE, 6, 1);
        CHECK_EQ(t.need, 1);
        CHECK_EQ(t.forced, 6);
    }
    // Both dice must be played when they can be. White's last checker on 10,
    // Red holding White's 4: 10/4 is blocked, but 10/9 then 9/3 plays both.
    clear(b);
    put(b, WHITE, {10, 1});
    put(b, RED, {21, 2, 6, 13});                        // Red's 21 = White's 4
    CHECK(valid(b));
    {
        Turn t;
        beginTurn(t, b, WHITE, 6, 1);
        CHECK_EQ(t.need, 2);
        CHECK(!stepAllowed(t, b, WHITE, 10, 6));        // 10/4 is blocked
        CHECK(stepAllowed(t, b, WHITE, 10, 1));         // 10/9, then 9/3
        CHECK_EQ(countPlays(b, WHITE, 6, 1), 1);
    }
    // A step that would strand the other die is refused. White on 8 and 7,
    // Red holding White's 1, a 6-4: 8/2 and 7/3 play both dice, but after
    // 8/4 the 6 has nowhere to go (7/1 is blocked), so 8/4 is not allowed.
    clear(b);
    put(b, WHITE, {8, 1, 7, 1});
    put(b, RED, {24, 2, 6, 13});                        // Red's 24 = White's 1
    CHECK(valid(b));
    {
        Turn t;
        beginTurn(t, b, WHITE, 6, 4);
        CHECK_EQ(t.need, 2);
        CHECK(!stepAllowed(t, b, WHITE, 8, 4));         // would leave the 6 unplayable
        CHECK(stepAllowed(t, b, WHITE, 7, 4));
        CHECK(stepAllowed(t, b, WHITE, 8, 6));
        CHECK(!stepAllowed(t, b, WHITE, 7, 6));
    }

    // Higher die rule: either die can be played but not both -> the higher.
    // White's lone checker on 12, Red holding White's 1..5: 12/7 and 12/8
    // are open, but 7/3 and 8/3 are not.
    clear(b);
    put(b, WHITE, {12, 1});
    put(b, RED, {24, 2, 23, 2, 22, 2, 21, 2, 20, 2, 12, 2, 6, 3});
    CHECK(valid(b));
    {
        Turn t;
        beginTurn(t, b, WHITE, 5, 4);
        CHECK_EQ(t.need, 1);
        CHECK_EQ(t.forced, 5);
        CHECK(stepAllowed(t, b, WHITE, 12, 5));
        CHECK(!stepAllowed(t, b, WHITE, 12, 4));
        CHECK_EQ(countPlays(b, WHITE, 5, 4), 1);
    }

    // Bearing off: exact, and a bigger die only from the highest point.
    clear(b);
    put(b, WHITE, {5, 2, 3, 1});
    put(b, RED, {6, 5, 5, 5, 4, 5});
    CHECK(valid(b));
    CHECK(canStep(b, WHITE, 5, 5));
    CHECK(canStep(b, WHITE, 5, 6));         // nothing above the 5
    CHECK(!canStep(b, WHITE, 3, 6));        // the 5 point is still occupied
    CHECK(canStep(b, WHITE, 3, 3));
    CHECK(canStep(b, WHITE, 5, 2));         // moving down inside is fine
    CHECK_EQ(countPlays(b, WHITE, 6, 6), 1);            // 5, 5, 3 off: three checkers, one way

    // The last checker: 6-4 with one on the 6 may bear off at once.
    clear(b);
    put(b, WHITE, {6, 1});
    put(b, RED, {6, 5, 5, 5, 4, 5});
    {
        Turn t;
        beginTurn(t, b, WHITE, 6, 4);
        CHECK_EQ(t.need, 2);                            // 6/2, then off with the 6
        CHECK(stepAllowed(t, b, WHITE, 6, 6));          // ... but straight off is fine too
        CHECK(stepAllowed(t, b, WHITE, 6, 4));
        // Looking at where it may go (which tries the winning step and takes
        // it back) leaves the turn as it was.
        Target tg[4];
        targets(t, b, WHITE, 6, tg);
        CHECK_EQ(t.need, 2);
        playStep(t, b, WHITE, 6, 6);
        CHECK(takeBack(t, b, WHITE));
        CHECK_EQ(t.need, 2);
        CHECK_EQ(t.dice.n, 2);
        playStep(t, b, WHITE, 6, 6);
        CHECK(turnDone(t));
        uint8_t w = 9;
        CHECK_EQ(result(b, w), 2);                      // (Red has borne none off: a gammon)
        CHECK_EQ(w, WHITE);
    }

    // Results.
    clear(b);
    put(b, WHITE, {});                                  // all fifteen off
    put(b, RED, {6, 5, 5, 5, 4, 5});
    uint8_t w;
    CHECK_EQ(result(b, w), 2);                          // gammon: Red has none off
    put(b, RED, {6, 5, 5, 5, 4, 4});
    CHECK_EQ(result(b, w), 1);
    clear(b);
    put(b, WHITE, {});
    put(b, RED, {20, 1, 6, 5, 5, 5, 4, 4});             // a Red checker in White's home board
    CHECK_EQ(result(b, w), 3);
    clear(b);
    put(b, WHITE, {});
    put(b, RED, {BAR, 1, 6, 5, 5, 5, 4, 4});
    CHECK_EQ(result(b, w), 3);
    clear(b);
    put(b, WHITE, {});
    put(b, RED, {18, 1, 6, 5, 5, 5, 4, 4});             // just outside it: gammon only
    CHECK_EQ(result(b, w), 2);

    // Contact.
    clear(b);
    put(b, WHITE, {6, 5, 5, 5, 4, 5});
    put(b, RED, {6, 5, 5, 5, 4, 5});
    CHECK(!contact(b));
    put(b, RED, {22, 1, 6, 5, 5, 5, 4, 4});             // a Red checker back on White's 3
    CHECK(contact(b));

    // Combined targets: from the start, 6-5 with a back checker runs 24/13
    // ("lover's leap") through 18.
    reset(b);
    {
        Turn t;
        beginTurn(t, b, WHITE, 6, 5);
        Target tg[4];
        uint8_t n = targets(t, b, WHITE, 24, tg);
        bool leap = false, six = false;
        for (uint8_t i = 0; i < n; i++) {
            if (tg[i].to == 13 && tg[i].n == 2 && tg[i].die[0] == 6) leap = true;
            if (tg[i].to == 18 && tg[i].n == 1) six = true;
        }
        CHECK(leap);
        CHECK(six);
        CHECK_EQ(n, 2);                                 // 24/19 is blocked
        // Doubles: 13/11, 13/9, 13/7, 13/5 with 2-2.
        beginTurn(t, b, WHITE, 2, 2);
        n = targets(t, b, WHITE, 13, tg);
        CHECK_EQ(n, 4);
        CHECK_EQ(tg[3].to, 5);
        CHECK_EQ(tg[3].n, 4);
    }
    // A hit on the way decides the route: White on 10, blot on 7 (3 away),
    // nothing on 6 (4 away): with 4-3 the combined move goes through the hit.
    clear(b);
    put(b, WHITE, {10, 1, 6, 5});
    put(b, RED, {18, 1, 6, 5, 5, 5, 4, 4});             // Red's 18 = White's 7: a blot
    CHECK(valid(b));
    {
        Turn t;
        beginTurn(t, b, WHITE, 4, 3);
        Target tg[4];
        uint8_t n = targets(t, b, WHITE, 10, tg);
        bool found = false;
        for (uint8_t i = 0; i < n; i++)
            if (tg[i].to == 3 && tg[i].n == 2) { found = true; CHECK_EQ(tg[i].die[0], 3); CHECK_EQ(tg[i].hits & 1, 1); }
        CHECK(found);
    }
}

static void testCrossCheck(int positions) {
    for (int i = 0; i < positions; i++) {
        Board b;
        randomPosition(b, i & 3);
        if (!valid(b)) { printf("FAIL generated an invalid position\n"); failures++; continue; }
        for (uint8_t d1 = 1; d1 <= 6; d1++)
            for (uint8_t d2 = d1; d2 <= 6; d2++)
                for (uint8_t s = 0; s < 2; s++) {
                    if (b.n[s][OFF] == CHECKERS) continue;
                    crossCheck(b, s, d2, d1);
                    if (failures > 20) return;
                }
        if (i % 16 == 0) {
            uint8_t s = (uint8_t)rnd(2), d1 = (uint8_t)(1 + rnd(6)), d2 = (uint8_t)(1 + rnd(6));
            if (b.n[s][OFF] != CHECKERS) validatorCheck(b, s, d1, d2);
        }
    }
    printf("cross-check: %ld rolls, %ld plays (%.1f a roll, most %ld), %ld repeats\n", totalRolls, totalPlays,
           totalRolls ? (double)totalPlays / (double)totalRolls : 0.0, maxPlays, totalDup);
}

// Whole games of random legal play: they end, and the board stays valid.
static void testGames(int games) {
    long turns = 0, longest = 0, passes = 0;
    int results[4] = {0, 0, 0, 0};
    Rng dice;
    dice.seed(12345, 1);
    for (int gme = 0; gme < games; gme++) {
        Board b;
        reset(b);
        uint8_t s = (uint8_t)(gme & 1), w = 0;
        long t = 0;
        while (!result(b, w)) {
            uint8_t d1 = dice.die(), d2 = dice.die();
            Plays g;
            begin(g, b, s, d1, d2);
            int n = 0;
            while (next(g, b)) n++;
            if (!g.need) passes++;
            int pick = rnd(n);
            begin(g, b, s, d1, d2);
            for (int k = 0; k <= pick; k++) next(g, b);
            if (!valid(b)) { printf("FAIL invalid board in a game: %s\n", show(b)); failures++; return; }
            s ^= 1;
            if (++t > 20000) { printf("FAIL a game did not end\n"); failures++; return; }
        }
        results[result(b, w)]++;
        turns += t;
        if (t > longest) longest = t;
    }
    printf("random games: %d, %.0f turns on average (longest %ld), %.1f%% passes; single %d gammon %d backgammon %d\n",
           games, (double)turns / games, longest, 100.0 * (double)passes / (double)turns, results[1], results[2],
           results[3]);
}

// The dice: faces, pairs and successive rolls uniform (chi-square, p = 0.001).
static void testDice() {
    Rng r;
    r.seed(7919, 1);
    const int N = 600000;
    long face[6] = {0}, pair[36] = {0}, lag[36] = {0};
    int prev = 0;
    long doubles = 0;
    for (int i = 0; i < N; i++) {
        int a = r.die() - 1, b = r.die() - 1;
        CHECK(a >= 0 && a < 6 && b >= 0 && b < 6);
        face[a]++; face[b]++;
        pair[a * 6 + b]++;
        lag[prev * 6 + a]++;
        prev = b;
        doubles += a == b;
    }
    double x = 0, e = 2.0 * N / 6;
    for (long f : face) x += (f - e) * (f - e) / e;
    CHECK(x < 20.52);                                   // 5 degrees of freedom
    double xp = 0, xl = 0;
    e = (double)N / 36;
    for (int i = 0; i < 36; i++) { xp += (pair[i] - e) * (pair[i] - e) / e; xl += (lag[i] - e) * (lag[i] - e) / e; }
    CHECK(xp < 66.62);                                  // 35 degrees of freedom
    CHECK(xl < 66.62);
    printf("dice: chi2 faces %.1f (< 20.5), pairs %.1f, successive %.1f (< 66.6); doubles %.2f%% (16.67%%)\n", x, xp,
           xl, 100.0 * (double)doubles / N);
    // Seeds one apart give unrelated first rolls; the same seed the same rolls.
    long first[6] = {0};
    for (uint32_t s = 0; s < 60000; s++) { Rng q; q.seed(s, 1); first[q.die() - 1]++; }
    double xf = 0;
    for (long f : first) xf += (f - 10000.0) * (f - 10000.0) / 10000.0;
    CHECK(xf < 20.52);
    Rng p, q;
    p.seed(77, 1); q.seed(77, 1);
    for (int i = 0; i < 100; i++) CHECK_EQ(p.die(), q.die());
    Rng o;
    p.seed(77, 1); o.seed(77, 2);                       // another stream: another sequence
    int same = 0;
    for (int i = 0; i < 600; i++) same += p.die() == o.die();
    CHECK(same < 160);
}

#ifdef WITH_MATCH
#include "test_match.h"
#endif

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "--quick");
    setvbuf(stdout, nullptr, _IONBF, 0);                // (a sanitizer trap must not swallow the output)
    testRules();
    testDice();
    testCrossCheck(quick ? 3000 : 50000);
    testGames(quick ? 200 : 3000);
#ifdef WITH_MATCH
    testMatch(quick);
#endif
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all tests passed\n");
    return 0;
}
