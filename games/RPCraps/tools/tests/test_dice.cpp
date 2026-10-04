// Host tests for the 3D dice physics (Dice3D.cpp, built with -DCHTEST:
// no drawing).
//
// 1. labelDie() makes real dice: every result is one of the 24 rotations of
//    a standard die, with the asked value on the asked face.
// 2. Throws at every power with many spins: they reach the back wall, stay on
//    the table, come to rest within the time cap, and the dice don't end up
//    inside each other.
// 3. fix(): after the replay, the faces on top show the rolled numbers, for
//    all 36 rolls.
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

static void testThrows() {
    int worst = 0, wallMiss = 0;
    int32_t zmin = 1 << 30, zmax = -(1 << 30), xmax = 0;
    int64_t zsum = 0, n = 0, tsum = 0;
    for (int power = 0; power < 256; power += 5) {
        for (int k = 0; k < 40; k++) {
            Pair p;
            memset(&p, 0, sizeof p);
            hold(p);
            p.d[0].yaw = (uint16_t)rnd(); p.d[0].pitch = (uint16_t)rnd(); p.d[0].roll = (uint16_t)rnd();
            p.d[1].yaw = (uint16_t)rnd(); p.d[1].pitch = (uint16_t)rnd(); p.d[1].roll = (uint16_t)rnd();
            release(p, (uint8_t)power, rnd());
            uint8_t hits = 0;
            int t = 0;
            for (; t < 400 && !atRest(p); t++) {
                step(p);
                hits |= p.hits;
                p.hits = 0;
                for (int i = 0; i < 2; i++) {
                    const Die &d = p.d[i];
                    CHECK(d.y > 0 && d.y < (80 << 8) && d.z > NEAR_Z - (4 << 8) && d.z < WALL_Z &&
                          d.x > -SIDE_X - (4 << 8) && d.x < SIDE_X + (4 << 8),
                          "power %d throw %d die %d out of the table at t %d (%d %d %d)", power, k, i, t,
                          d.x >> 8, d.y >> 8, d.z >> 8);
                }
                if (fails > 20) return;
            }
            if (t > worst) worst = t;
            tsum += t;
            for (int i = 0; i < 2; i++) {
                int32_t z = p.d[i].z >> 8, x = p.d[i].x >> 8;
                if (z < zmin) zmin = z;
                if (z > zmax) zmax = z;
                if (x < 0) x = -x;
                if (x > xmax) xmax = x;
                zsum += z; n++;
            }
            CHECK(atRest(p) && t <= 240, "power %d throw %d: not at rest after %d ticks", power, k, t);
            if (!(hits & HIT_WALL)) wallMiss++;
            int32_t dx = p.d[0].x - p.d[1].x, dz = p.d[0].z - p.d[1].z;
            if (dx < 0) dx = -dx;
            if (dz < 0) dz = -dz;
            CHECK(dx >= (14 << 8) || dz >= (14 << 8), "power %d throw %d: dice inside each other (%d, %d)",
                  power, k, dx >> 8, dz >> 8);
            for (int i = 0; i < 2; i++) {
                const Die &d = p.d[i];
                CHECK(d.y == (HALF << 8) && (d.pitch & 0x3FFF) == 0 && (d.roll & 0x3FFF) == 0,
                      "power %d throw %d die %d: not lying flat", power, k, i);
            }
        }
    }
    CHECK(wallMiss == 0, "%d throws never reached the back wall", wallMiss);
    printf("dice: slowest throw %d ticks (%.1f s), average %.1f s; rest z %d..%d (avg %d), |x| <= %d\n", worst,
           worst / 60.0, tsum / 60.0 / (n / 2), (int)zmin, (int)zmax, (int)(zsum / n), (int)xmax);
}

static void testFix() {
    for (int a = 1; a <= 6; a++)
        for (int b = 1; b <= 6; b++)
            for (int k = 0; k < 20; k++) {
                Pair p;
                memset(&p, 0, sizeof p);
                hold(p);
                p.d[0].pitch = (uint16_t)rnd(); p.d[1].roll = (uint16_t)rnd();
                release(p, (uint8_t)rnd(), rnd());
                uint8_t before[2][6];
                memcpy(before[0], p.d[0].label, 6); memcpy(before[1], p.d[1].label, 6);
                for (int i = 0; i < 2; i++)
                    for (int f = 0; f < 6; f++) CHECK(before[i][f] >= 1 && before[i][f] <= 6, "die %d has no pips in the hand", i);
                fix(p, (uint8_t)a, (uint8_t)b);
                for (int t = 0; t < 400 && !atRest(p); t++) step(p);
                uint8_t ua = p.d[0].label[upFace(p.d[0])], ub = p.d[1].label[upFace(p.d[1])];
                CHECK(ua == a && ub == b, "rolled %d-%d, the dice show %d-%d", a, b, ua, ub);
            }
}

int main() {
    testLabels();
    testThrows();
    testFix();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
