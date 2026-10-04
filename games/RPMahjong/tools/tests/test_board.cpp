// Host tests for the game logic: layouts, the free rule, deals, matching,
// undo, shuffles, saved games and the cursor's hops.
//
//   rpgame test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../MahjongBoard.h"
#include "../../src/game/Layouts.h"
#include "../../Nav.h"

using namespace board;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); failures++; } } while (0)

static uint32_t trng = 0xC0FFEEu;
static uint32_t trnd(uint32_t n) { trng ^= trng << 13; trng ^= trng >> 17; trng ^= trng << 5; return trng % n; }

static void deal(uint8_t l, uint32_t seed, uint8_t budget = 255) {
    layout = l;
    dealBegin(seed);
    while (!dealStep(budget)) {}
}

// The rule, the slow way: nothing on any part of it, and a side open.
static bool freeRef(uint8_t i) {
    bool l = false, r = false;
    for (uint8_t j = 0; j < count; j++) {
        if (j == i || !present(j)) continue;
        int dx = pos[j].x2 - pos[i].x2, dy = pos[j].y2 - pos[i].y2, dz = pos[j].z - pos[i].z;
        if (dz == 1 && abs(dx) < 2 && abs(dy) < 2) return false;
        if (dz == 0 && abs(dy) < 2) { l |= dx == -2; r |= dx == 2; }
    }
    return !(l && r);
}

// ---------------------------------------------------------------------------
static void testLayouts() {
    for (uint8_t l = 0; l < LAYOUTS; l++) {
        load(l);
        CHECK_EQ(count, LAYOUT_TILES[l]);
        CHECK_EQ(left, count);
        CHECK(count % 2 == 0 && count <= MAX_TILES);
        for (uint8_t i = 0; i < count; i++) {
            CHECK(pos[i].x2 <= 28 && pos[i].y2 <= 14 && pos[i].z <= 4);
            // On the screen (Stage.cpp: the pile starts at 4, 14).
            CHECK(4 + px(i) >= 0 && 4 + px(i) + 10 <= 128 && 14 + py(i) >= 12 && 14 + py(i) + 14 <= 116);
            for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
                int dx = pos[j].x2 - pos[i].x2, dy = pos[j].y2 - pos[i].y2;
                bool same = pos[i].z == pos[j].z;
                CHECK(!(same && abs(dx) < 2 && abs(dy) < 2));       // no two overlap
                CHECK(pos[j].z >= pos[i].z);                        // lower layers first
                // A tile's shadow falls right and down: whatever it can
                // touch on its own layer is drawn after it.
                if (same && ((dx == -2 && abs(dy) < 2) || (dy == -2 && abs(dx) < 2))) CHECK(false);
            }
        }
        for (uint8_t i = 0; i < count; i++) CHECK_EQ(isFree(i), freeRef(i));
    }
}

static uint32_t hashDeal() {
    uint32_t h = 2166136261u;
    for (uint8_t i = 0; i < count; i++) h = (h ^ face[i]) * 16777619u;
    return h;
}

static void testDeals() {
    int most = 0;
    for (uint8_t l = 0; l < LAYOUTS; l++) {
        for (uint32_t seed = 1; seed <= 10000; seed++) {
            deal(l, seed * 2654435761u);
            CHECK(!dealing());
            CHECK_EQ(left, count);
            uint8_t n[FACES] = {0};
            for (uint8_t i = 0; i < count; i++) { CHECK(face[i] < FACES); n[face[i]]++; }
            if (count == MAX_TILES)
                for (uint8_t f = 0; f < FACES; f++) CHECK_EQ(n[f], f < FLOWER ? 4 : 1);
            CHECK(pairs() > 0);
            // The deal's own order clears the table, and pays.
            uint8_t order[MAX_TILES];
            memcpy(order, dealOrder, sizeof order);
            for (uint8_t k = 0; k < count / 2; k++) {
                uint8_t list[MAX_FREE], nf = freeList(list);
                if (nf > most) most = nf;
                if (seed <= 50) for (uint8_t i = 0; i < count; i++) if (present(i)) CHECK_EQ(isFree(i), freeRef(i));
                CHECK(!cleared());
                if (!match(order[2 * k], order[2 * k + 1])) { CHECK(false); break; }
            }
            CHECK(cleared());
            CHECK(!stuck());
            CHECK(bonus >= CLEAR_BONUS);
            CHECK(chips >= bonus + count / 2 * PAIR_PAYS);
            if (failures > 20) return;
        }
    }
    CHECK(most <= MAX_FREE);
    printf("deals: 10000 per layout clear; most free at once %d\n", most);

    // The same deal however the work is split across frames, and the same
    // as ever: saved games replay the deal from its seed.
    static const uint32_t GOLDEN[LAYOUTS] = {0xB59EF533u, 0x1C5D5B49u, 0x19007C31u, 0x817F5D21u};
    for (uint8_t l = 0; l < LAYOUTS; l++) {
        deal(l, 12345);
        uint32_t h = hashDeal();
        deal(l, 12345, 1);
        CHECK_EQ(hashDeal(), h);
        deal(l, 12345, 7);
        CHECK_EQ(hashDeal(), h);
        deal(l, 12346);
        CHECK(hashDeal() != h);
        if (h != GOLDEN[l]) { printf("FAIL deal %d seed 12345: hash 0x%08Xu (a changed deal breaks saved games: bump save VERSION)\n", l, h); failures++; }
    }
}

static void testRules() {
    deal(0, 99);
    uint8_t list[MAX_FREE], n = freeList(list);
    CHECK(n >= 2);
    // Not itself, not a tile that is not free, not a different face.
    CHECK(!canMatch(list[0], list[0]));
    for (uint8_t i = 0; i < count; i++) {
        if (isFree(i)) continue;
        for (uint8_t k = 0; k < n; k++) CHECK(!canMatch(i, list[k]));
    }
    for (uint8_t a = 0; a < n; a++)
        for (uint8_t b = 0; b < n; b++)
            CHECK_EQ(canMatch(list[a], list[b]), a != b && group(face[list[a]]) == group(face[list[b]]));
    // Any flower with any flower, any season with any season, never across.
    CHECK_EQ(group(34), group(37));
    CHECK_EQ(group(38), group(41));
    CHECK(group(37) != group(38));
    for (uint8_t f = 0; f < FLOWER; f++) CHECK_EQ(group(f), f);
    // Pairs counted = pairs found.
    uint8_t found = 0;
    for (uint8_t a = 0; a < n; a++) for (uint8_t b = (uint8_t)(a + 1); b < n; b++) found += canMatch(list[a], list[b]);
    CHECK_EQ(pairs(), found);
    uint8_t ha, hb;
    CHECK(hint(ha, hb) && canMatch(ha, hb));

    // The streak: a pair straight after another pays more, up to x5; let the
    // clock run out and it starts over.
    deal(0, 7);
    uint8_t order[MAX_TILES];
    memcpy(order, dealOrder, sizeof order);
    int32_t before = chips;
    for (uint8_t k = 0; k < 7; k++) {
        bool dbl = group(face[order[2 * k]]) >= FLOWER;
        before = chips;
        CHECK(match(order[2 * k], order[2 * k + 1]));
        uint8_t mult = k < MAX_STREAK ? (uint8_t)(k + 1) : MAX_STREAK;
        CHECK_EQ(streak, mult);
        CHECK_EQ(chips - before, mult * PAIR_PAYS * (dbl ? 2 : 1));
        CHECK_EQ(lastPay(), chips - before);
        tick();
    }
    for (int f = 0; f < STREAK_FRAMES; f++) tick();
    CHECK_EQ(streakT, 0);
    CHECK(match(order[14], order[15]));
    CHECK_EQ(streak, 1);
    CHECK_EQ(secs(), (7 + STREAK_FRAMES) / 60);
    // Chips never go below nothing.
    chips = 10;
    spend(HINT_COST);
    CHECK_EQ(chips, 0);
}

struct Snap { uint8_t here[MAX_TILES], faces[MAX_TILES], left, pairs; int32_t chips; };
static Snap snap() {
    Snap s;
    memset(&s, 0, sizeof s);
    for (uint8_t i = 0; i < count; i++) { s.here[i] = present(i); s.faces[i] = face[i]; }
    s.left = left; s.pairs = pairs(); s.chips = chips;
    return s;
}
static bool same(const Snap &a, const Snap &b) { return !memcmp(&a, &b, sizeof a); }

// A random player: takes any pair there is. Returns false when stuck.
static bool playOne() {
    uint8_t list[MAX_FREE], n = freeList(list), cand[MAX_FREE * 4][2];
    int nc = 0;
    for (uint8_t a = 0; a < n; a++)
        for (uint8_t b = (uint8_t)(a + 1); b < n && nc < MAX_FREE * 4; b++)
            if (canMatch(list[a], list[b])) { cand[nc][0] = list[a]; cand[nc][1] = list[b]; nc++; }
    if (!nc) return false;
    int k = (int)trnd((uint32_t)nc);
    CHECK(match(cand[k][0], cand[k][1]));
    return true;
}

static void testUndo() {
    for (uint32_t g = 0; g < 300; g++) {
        deal((uint8_t)(g % LAYOUTS), 1000 + g);
        CHECK(!canUndo());
        Snap stack[MAX_TILES / 2 + 1];
        int depth = 0;
        for (int step = 0; step < 400 && !cleared(); step++) {
            if (depth && trnd(4) == 0) {
                uint8_t a, b;
                CHECK(canUndo());
                CHECK(undo(a, b));
                CHECK(present(a) && present(b));
                CHECK_EQ(streak, 0);
                depth--;
                CHECK(same(snap(), stack[depth]));      // the table and the chips, as they were
            } else {
                stack[depth] = snap();
                if (!playOne()) break;
                depth++;
            }
        }
        if (cleared()) CHECK(!canUndo());
    }
    // Chips are what the pairs paid, whatever was undone on the way.
    deal(0, 5);
    uint8_t order[MAX_TILES], a, b;
    memcpy(order, dealOrder, sizeof order);
    CHECK(match(order[0], order[1]));
    CHECK_EQ(chips, PAIR_PAYS * (group(face[order[0]]) >= FLOWER ? 2 : 1));
    CHECK(undo(a, b));
    CHECK_EQ(chips, 0);
    CHECK(!canUndo());
}

static void testShuffle() {
    int stuckGames = 0, dealt = 0, impossible = 0;
    for (uint32_t g = 0; g < 2000; g++) {
        deal((uint8_t)(g % LAYOUTS), 5000 + g);
        while (playOne()) {}
        if (cleared()) continue;
        CHECK(stuck());
        stuckGames++;
        Snap before = snap();
        uint8_t n[FACES] = {0}, m[FACES] = {0};
        for (uint8_t i = 0; i < count; i++) if (present(i)) n[face[i]]++;
        uint8_t was = shufflesLeft();
        CHECK(shuffleBegin());
        CHECK(dealing());
        while (!dealStep(3)) {}
        if (shuffleFailed()) {
            // Nothing changes, and nothing is charged.
            impossible++;
            CHECK(same(snap(), before));
            CHECK_EQ(shufflesLeft(), was);
            continue;
        }
        dealt++;
        CHECK_EQ(shufflesLeft(), was - 1);
        CHECK_EQ(left, before.left);
        for (uint8_t i = 0; i < count; i++) {
            CHECK_EQ(present(i), before.here[i]);                   // the tiles stay where they are
            if (present(i)) m[face[i]]++;
        }
        CHECK(!memcmp(n, m, sizeof n));                             // the same faces, moved about
        CHECK(pairs() > 0);
        CHECK(!canUndo());                                          // not back past a shuffle
        uint8_t order[MAX_TILES], k = 0;
        memcpy(order, dealOrder, sizeof order);
        for (uint8_t todo = left / 2; todo; todo--, k++) CHECK(match(order[2 * k], order[2 * k + 1]));
        CHECK(cleared());
    }
    CHECK(stuckGames > 100 && dealt > 100);
    printf("shuffles: %d stuck tables, %d dealt again and cleared, %d could not be\n", stuckGames, dealt, impossible);

    // Two tiles, one on the other, cannot be dealt: the shuffle says so.
    static const uint8_t STACK[] = {0, 0, 0, 1,  1, 0, 0, 1,  0, 0, 4, 1,  0, 0, 8, 1,  0xFF};
    testLayout = STACK;
    deal(0, 3);
    CHECK_EQ(count, 4);
    uint8_t lone[2], k = 0;
    for (uint8_t i = 0; i < count; i++) { face[i] = 0; if (pos[i].x2) lone[k++] = i; }
    CHECK(match(lone[0], lone[1]));
    CHECK(stuck());
    int32_t had = chips;
    CHECK(shuffleBegin());
    while (!dealStep(1)) {}
    CHECK(shuffleFailed());
    CHECK(stuck());
    CHECK_EQ(chips, had);
    CHECK_EQ(shufflesLeft(), MAX_SHUFFLES);
    testLayout = nullptr;

    // Only so many shuffles a game.
    deal(0, 77);
    for (uint8_t i = 0; i < MAX_SHUFFLES; i++) {
        chips = 1000;
        CHECK(shuffleBegin());
        while (!dealStep(255)) {}
        CHECK(!shuffleFailed());
        CHECK_EQ(chips, 1000 - SHUFFLE_COST);
    }
    CHECK(!shuffleBegin());
}

static void testSave() {
    for (uint32_t g = 0; g < 400; g++) {
        deal((uint8_t)(g % LAYOUTS), 9000 + g);
        int steps = (int)trnd(80);
        for (int s = 0; s < steps && !cleared(); s++) {
            if (trnd(11) == 0 && shuffleBegin()) { while (!dealStep(5)) {} continue; }
            if (trnd(9) == 0 && canUndo()) { uint8_t a, b; undo(a, b); continue; }
            if (!playOne()) {
                if (!shuffleBegin()) break;
                while (!dealStep(5)) {}
                if (shuffleFailed()) break;
            }
            for (uint32_t f = trnd(200); f; f--) tick();
        }
        if (cleared()) continue;
        Record r;
        save(r);
        Snap was = snap();
        uint16_t sec = secs();
        uint8_t st = streak, sh = shufflesLeft();
        bool couldUndo = canUndo();
        load((uint8_t)((g + 1) % LAYOUTS));          // something else on the table
        CHECK(restore(r));
        CHECK(same(snap(), was));
        CHECK_EQ(secs(), sec);
        CHECK_EQ(streak, st);
        CHECK_EQ(shufflesLeft(), sh);
        CHECK_EQ(canUndo(), couldUndo);
        // ... and it plays on from there.
        if (canUndo()) { uint8_t a, b; CHECK(undo(a, b)); }
        while (playOne()) {}
        // A record that is not a game is refused.
        Record bad = r;
        bad.layout = LAYOUTS;
        CHECK(!restore(bad));
        bad = r;
        bad.n = HIST + 1;
        CHECK(!restore(bad));
        if (r.n && r.ab[0][0] != NONE) {
            bad = r;
            bad.ab[0][1] = bad.ab[0][0];             // a tile with itself
            CHECK(!restore(bad));
        }
        bad = r;
        bad.seed ^= 0x5A5A5A5Au;                     // another deal: the pairs taken no longer match
        if (r.n > 8) CHECK(!restore(bad));
    }
    CHECK(sizeof(Record) <= 212);                    // with the options and stats, one 256-byte page
}

// From every free tile, the D-pad reaches every other; LEFT and RIGHT alone do.
static void testNav() {
    int worst = 0;
    long states = 0;
    for (uint32_t g = 0; g < 400; g++) {
        deal((uint8_t)(g % LAYOUTS), 3000 + g);
        do {
            uint8_t list[MAX_FREE], n = freeList(list);
            if (n < 2) continue;
            states++;
            static const int8_t DX[4] = {-1, 1, 0, 0}, DY[4] = {0, 0, -1, 1};
            for (uint8_t d = 0; d < 4; d++) {
                uint8_t t = nav::step(list, n, list[0], DX[d], DY[d]);
                CHECK(t != NONE && t != list[0] && isFree(t));
            }
            // RIGHT goes round them all and comes back; LEFT undoes RIGHT.
            uint8_t at = list[0], seen = 0;
            do {
                uint8_t next = nav::step(list, n, at, 1, 0);
                CHECK_EQ(nav::step(list, n, next, -1, 0), at);
                at = next;
            } while (++seen < n && at != list[0]);
            CHECK_EQ(seen, n);
            CHECK_EQ(at, list[0]);
            // Fewest presses between any two (all four directions).
            for (uint8_t s = 0; s < n; s += 5) {
                uint8_t dist[MAX_TILES], q[MAX_FREE], qh = 0, qt = 0;
                memset(dist, 0xFF, sizeof dist);
                dist[list[s]] = 0;
                q[qt++] = list[s];
                while (qh < qt) {
                    uint8_t c = q[qh++];
                    for (uint8_t d = 0; d < 4; d++) {
                        uint8_t t = nav::step(list, n, c, DX[d], DY[d]);
                        if (dist[t] == 0xFF) { dist[t] = (uint8_t)(dist[c] + 1); q[qt++] = t; if (dist[t] > worst) worst = dist[t]; }
                    }
                }
                CHECK_EQ(qt, n);
            }
            // From a tile that has just been taken: the nearest one left.
            CHECK(isFree(nav::nearest(list, n, list[n / 2])));
        } while (playOne());
    }
    printf("cursor: %ld tables, every free tile reached; at most %d presses between two\n", states, worst);
}

// Anything, in any order.
static void testFuzz() {
    for (uint32_t g = 0; g < 200; g++) {
        deal((uint8_t)trnd(LAYOUTS), trnd(0xFFFFFFFFu));
        for (int s = 0; s < 600; s++) {
            uint8_t a = (uint8_t)trnd(256), b = (uint8_t)trnd(256), x, y;
            switch (trnd(9)) {
                case 0: case 1: case 2: if (match(a, b)) CHECK(a < count && b < count); break;
                case 3: playOne(); break;
                case 4: undo(x, y); break;
                case 5: if (shuffleBegin()) { CHECK(!match(a, b)); while (!dealStep((uint8_t)(1 + trnd(9)))) {} } break;
                case 6: if (hint(x, y)) CHECK(canMatch(x, y)); break;
                case 7: tick(); spend((uint16_t)trnd(300)); break;
                case 8: { Record r; save(r); if (!cleared()) CHECK(restore(r)); break; }
            }
            CHECK(chips >= 0);
            CHECK(left <= count && left % 2 == 0);
            uint8_t on = 0;
            for (uint8_t i = 0; i < count; i++) { on += present(i); if (isFree(i)) CHECK(present(i)); }
            CHECK_EQ(on, left);
            CHECK_EQ(cleared(), left == 0);
        }
    }
}

int main() {
    testLayouts();
    testDeals();
    testRules();
    testUndo();
    testShuffle();
    testSave();
    testNav();
    testFuzz();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("all tests passed\n");
    return 0;
}
