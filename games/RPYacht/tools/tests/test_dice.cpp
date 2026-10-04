// Host tests for the 3D dice physics (Dice3D.cpp, built with -DCHTEST:
// no drawing).
//
// 1. labelDie() makes real dice: every result is one of the 24 rotations of
//    a standard die, with the asked value on the asked face.
// 2. Throws at every power, for every set of dice kept back: they reach the
//    back wall, stay in the tray, come to rest within the time cap, and no
//    two dice end up inside each other.
// 3. fix(): after the replay, the thrown dice show the rolled numbers and the
//    kept ones have not moved.
#include <stdio.h>
#include <string.h>
#include "../../Dice3D.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

using namespace d3;

// Quarter turns of a labelling (faces +X -X +Y -Y +Z -Z). About X: +Y -> +Z
// -> -Y -> -Z -> +Y. About Y: +Z -> +X -> -Z -> -X -> +Z.
static void turnX(uint8_t *L) { uint8_t t = L[2]; L[2] = L[5]; L[5] = L[3]; L[3] = L[4]; L[4] = t; }
static void turnY(uint8_t *L) { uint8_t t = L[4]; L[4] = L[1]; L[1] = L[5]; L[5] = L[0]; L[0] = t; }

static uint8_t rots[24][6];
static int nRots = 0;

static void allRotations() {
    uint8_t base[6] = {2, 5, 1, 6, 3, 4};
    memcpy(rots[nRots++], base, 6);
    for (int i = 0; i < nRots; i++) {
        for (int g = 0; g < 2; g++) {
            uint8_t L[6];
            memcpy(L, rots[i], 6);
            if (g) turnY(L); else turnX(L);
            bool seen = false;
            for (int k = 0; k < nRots; k++) seen |= !memcmp(rots[k], L, 6);
            if (!seen && nRots < 24) memcpy(rots[nRots++], L, 6);
        }
    }
}

static void testLabels() {
    allRotations();
    CHECK(nRots == 24, "24 rotations of a die (%d)", nRots);
    for (uint8_t up = 0; up < 6; up++)
        for (uint8_t v = 1; v <= 6; v++)
            for (uint8_t spin = 0; spin < 4; spin++) {
                Die d;
                memset(&d, 0, sizeof d);
                labelDie(d, up, v, spin);
                bool real = false;
                for (int k = 0; k < 24; k++) real |= !memcmp(rots[k], d.label, 6);
                CHECK(d.label[up] == v, "up %d v %d spin %d: shows %d", up, v, spin, d.label[up]);
                CHECK(real, "up %d v %d spin %d: not a real die (%d %d %d %d %d %d)", up, v, spin,
                      d.label[0], d.label[1], d.label[2], d.label[3], d.label[4], d.label[5]);
            }
}

static uint32_t r = 1;
static uint32_t rnd() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }

static void scramble(Dice &p) {
    for (int i = 0; i < N; i++) { p.d[i].yaw = (uint16_t)rnd(); p.d[i].pitch = (uint16_t)rnd(); p.d[i].roll = (uint16_t)rnd(); }
}

// Every way of keeping dice back (all but keeping all five), at every power.
static void testThrows() {
    int worst = 0, wallMiss = 0, throws = 0;
    int32_t zmin = 1 << 30, zmax = -(1 << 30), xmax = 0;
    int64_t zsum = 0, n = 0, tsum = 0;
    for (int kept = 0; kept < 31; kept++) {
        for (int power = 0; power < 256; power += 15) {
            for (int k = 0; k < 12; k++) {
                Dice p;
                memset(&p, 0, sizeof p);
                hold(p, (uint8_t)kept);
                for (int i = 0; i < N; i++)
                    for (int j = i + 1; j < N; j++) {
                        if (p.d[i].state == KEPT || p.d[j].state == KEPT) continue;
                        int32_t dx = p.d[i].x - p.d[j].x, dy = p.d[i].y - p.d[j].y;
                        CHECK((dx < 0 ? -dx : dx) >= (18 << 8) || (dy < 0 ? -dy : dy) >= (18 << 8),
                              "kept %d: dice %d and %d touch in the hand", kept, i, j);
                    }
                scramble(p);
                release(p, (uint8_t)power, rnd());
                uint8_t hits = 0;
                int t = 0;
                for (; t < 400 && !atRest(p); t++) {
                    step(p);
                    hits |= p.hits;
                    p.hits = 0;
                    for (int i = 0; i < N; i++) {
                        const Die &d = p.d[i];
                        if (d.state == KEPT) continue;
                        CHECK(d.y > 0 && d.y < (90 << 8) && d.z > NEAR_Z - (4 << 8) && d.z < WALL_Z &&
                              d.x > -SIDE_X - (4 << 8) && d.x < SIDE_X + (4 << 8),
                              "kept %d power %d throw %d die %d out of the tray at t %d (%d %d %d)", kept, power, k, i,
                              t, d.x >> 8, d.y >> 8, d.z >> 8);
                    }
                    if (fails > 20) return;
                }
                throws++;
                if (t > worst) worst = t;
                tsum += t;
                CHECK(atRest(p) && t <= 260, "kept %d power %d throw %d: not at rest after %d ticks", kept, power, k, t);
                if (!(hits & HIT_WALL)) { wallMiss++; printf("no wall: kept %d power %d throw %d\n", kept, power, k); }
                for (int i = 0; i < N; i++) {
                    const Die &d = p.d[i];
                    if (d.state == KEPT) continue;
                    int32_t z = d.z >> 8, x = d.x >> 8;
                    if (z < zmin) zmin = z;
                    if (z > zmax) zmax = z;
                    if (x < 0) x = -x;
                    if (x > xmax) xmax = x;
                    zsum += z; n++;
                    CHECK(d.y == (HALF << 8) && (d.pitch & 0x3FFF) == 0 && (d.roll & 0x3FFF) == 0,
                          "kept %d power %d throw %d die %d: not lying flat", kept, power, k, i);
                    for (int j = i + 1; j < N; j++) {
                        if (p.d[j].state == KEPT) continue;
                        int32_t dx = d.x - p.d[j].x, dz = d.z - p.d[j].z;
                        if (dx < 0) dx = -dx;
                        if (dz < 0) dz = -dz;
                        CHECK(dx >= ((EDGE - 2) << 8) || dz >= ((EDGE - 2) << 8), "kept %d power %d throw %d: dice %d and %d inside each other (%d, %d)",
                              kept, power, k, i, j, dx >> 8, dz >> 8);
                    }
                }
            }
        }
    }
    CHECK(wallMiss == 0, "%d throws never reached the back wall", wallMiss);
    printf("dice: %d throws, slowest %d ticks (%.1f s), average %.1f s; rest z %d..%d (avg %d), |x| <= %d\n", throws,
           worst, worst / 60.0, tsum / 60.0 / throws, (int)zmin, (int)zmax, (int)(zsum / n), (int)xmax);
}

// After the replay the thrown dice show what was rolled; kept dice are untouched.
static void testFix() {
    for (int k = 0; k < 3000; k++) {
        Dice p;
        memset(&p, 0, sizeof p);
        uint8_t kept = (uint8_t)(rnd() % 31), v[N];
        hold(p, kept);
        scramble(p);
        for (int i = 0; i < N; i++) v[i] = (uint8_t)(1 + rnd() % 6);
        release(p, (uint8_t)rnd(), rnd());
        for (int i = 0; i < N; i++) {
            if (p.d[i].state == KEPT) continue;
            for (int f = 0; f < 6; f++) CHECK(p.d[i].label[f] >= 1 && p.d[i].label[f] <= 6, "die %d has no pips in the hand", i);
        }
        Die keptWas[N];
        memcpy(keptWas, p.d, sizeof keptWas);
        fix(p, v);
        for (int t = 0; t < 400 && !atRest(p); t++) step(p);
        for (int i = 0; i < N; i++) {
            if (kept >> i & 1) { CHECK(!memcmp(&keptWas[i], &p.d[i], sizeof(Die)), "kept die %d moved", i); continue; }
            uint8_t up = p.d[i].label[upFace(p.d[i])];
            CHECK(up == v[i], "die %d rolled %d, shows %d", i, v[i], up);
        }
    }
}

int main() {
    testLabels();
    testThrows();
    testFix();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
