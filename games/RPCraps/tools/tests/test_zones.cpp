// Host tests for the layout's spots and the cursor (Zones.cpp).
//
// 1. Every bet a table offers has a spot, and every spot's chips sit inside
//    the screen's felt (or the bar).
// 2. From every usable spot, the D-pad reaches every other usable spot -
//    with and without come points up (their odds spots come and go).
// 3. DOWN from the bottom of the felt lands in the bar; ROLL is reachable.
#include <stdio.h>
#include <string.h>
#include "../../Craps.h"
#include "../../Zones.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void reach(const Craps &g, const char *what) {
    uint8_t n = zones::count(g.opt.table);
    static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
    for (uint8_t from = 0; from < n; from++) {
        if (!zones::usable(g, from)) continue;
        bool seen[64] = {false};
        uint8_t q[64], qh = 0, qt = 0;
        seen[from] = true; q[qt++] = from;
        while (qh < qt) {
            uint8_t z = q[qh++];
            for (int d = 0; d < 4; d++) {
                uint8_t t = zones::nearest(g, z, DX[d], DY[d]);
                CHECK(t == 0xFF || zones::usable(g, t), "%s: moved onto an unusable spot", what);
                if (t != 0xFF && !seen[t]) { seen[t] = true; q[qt++] = t; }
            }
        }
        for (uint8_t to = 0; to < n; to++)
            if (zones::usable(g, to)) CHECK(seen[to], "%s: spot %d can't reach spot %d", what, from, to);
    }
}

static void testTable(uint8_t table) {
    Craps g;
    memset(&g, 0, sizeof g);
    g.opt.table = table;
    g.newGame();
    const char *name = table == TABLE_BEGINNER ? "beginner" : "classic";
    uint8_t n = zones::count(table);
    CHECK(n <= 64, "%s: too many spots", name);
    for (uint8_t b = 0; b < BET_COUNT; b++) {
        if (!g.onTable(b)) continue;
        int x, y;
        CHECK(zones::anchor(table, b, x, y), "%s: bet %d has no spot", name, b);
        CHECK(x >= 5 && x <= 122 && y >= 47 && y <= 110, "%s: bet %d's chips at %d,%d are off the felt", name, b, x, y);
    }
    for (uint8_t i = 0; i < n; i++) {
        const Zone &z = zones::at(table, i);
        CHECK(z.x + z.w <= 128 && z.y + z.h <= 128, "%s: spot %d runs off the screen", name, i);
        CHECK(z.ax >= z.x && z.ax < z.x + z.w + 1 && z.ay >= z.y - 1 && z.ay <= z.y + z.h,
              "%s: spot %d's anchor %d,%d is outside it", name, i, z.ax, z.ay);
    }
    CHECK(zones::find(table, Z_ROLL) != 0xFF, "%s: no ROLL", name);
    reach(g, name);
    // With come points up, their odds spots join in.
    if (table == TABLE_CLASSIC) {
        for (uint8_t i = 0; i < 6; i++) g.bet[COME4 + i] = 5;
        reach(g, "classic with come points");
    }
    // DOWN from the line lands in the bar.
    uint8_t pass = zones::find(table, PASS), down = zones::nearest(g, pass, 0, 1);
    CHECK(down != 0xFF && zones::inBar(table, down), "%s: DOWN from the pass line leaves the felt", name);
}

int main() {
    testTable(TABLE_CLASSIC);
    testTable(TABLE_BEGINNER);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
