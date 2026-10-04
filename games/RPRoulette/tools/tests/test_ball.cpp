// Host tests for the ball simulation and landing solver (Ball.cpp).
//   python tools/tests/run_ball_tests.py            all checks + histograms
//   test_ball.exe --trace SEED N QUICK TARGET       one spin, a line per tick
//   test_ball.exe --stats [SPINS]                   tuning tables only
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../Ball.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; if (fails < 40) printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { fails++; if (fails < 40) printf("FAIL %s:%d  %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

using namespace ball;

// The pace windows the user asked for (ticks at 60 Hz).
static const int FLICK_TO_LAND_MIN[2] = {216, 120}, FLICK_TO_LAND_MAX[2] = {276, 162};
static const int WAIT_TICKS[2] = {24, 14};
static const int PHASE_SHARE_LO[4] = {45, 5, 18, 3}, PHASE_SHARE_HI[4] = {65, 16, 40, 8};

static uint32_t lcg = 12345;
static uint32_t rand32() { lcg = lcg * 1664525u + 1013904223u; return lcg ^ (lcg >> 16); }

// Everything one spin did, for checks and tables.
struct Spin {
    int flick, leave, rotor, settle, land;          // tick of each phase start (-1: never)
    int pocket, hits, frets, backs, bounces, rolls, flicks, lands, leaves;
    int maxRel;                                     // max |v| and |dpsi| on the rotor
    int maxRotorW;                                  // max |rotor speed|, pocket units per tick
    int trackTurnsQ16;                              // travelled on the track, turn-Q16
    int clampHit;                                   // entry speed was capped
    int idle;                                       // ticks at rest before SETTLE (the min clamp)
    int lastHop;                                    // ticks after the flick of the last fret hop
    int steps;
    uint32_t hash;
};

static uint32_t mix(uint32_t h, uint32_t v) { h ^= v; h *= 0x01000193u; return h; }

static uint32_t hashBall(const Ball &b, uint32_t h, uint8_t ev) {
    const int32_t v32[] = {b.a, b.w, b.rho, b.rw, b.psi, b.v, b.dec};
    for (int32_t x : v32) h = mix(h, (uint32_t)x);
    const int32_t vrz[] = {b.r, b.vr, b.z, b.vz};
    for (int32_t x : vrz) h = mix(h, (uint32_t)x);
    h = mix(h, b.rng); h = mix(h, b.t | (uint32_t)b.phaseT << 16);
    h = mix(h, b.ph | b.hits << 8 | b.pocket << 16 | (uint32_t)b.defl << 24);
    return mix(h, ev);
}

static int32_t pocketDelta(int32_t d, int32_t T) {    // signed shortest difference mod T
    if (d > T / 2) d -= T;
    if (d < -T / 2) d += T;
    return d;
}

// Run a ball to DONE (plus a few ticks of riding), recording everything.
static Spin run(Ball &b, FILE *trace = nullptr, int after = 6) {
    Spin s;
    memset(&s, 0, sizeof s);
    s.flick = s.leave = s.rotor = s.settle = s.land = -1;
    s.hash = 0x811C9DC5u;
    const int32_t T = (int32_t)b.n << 16;
    int32_t prevPsi = 0;
    uint8_t prevPh = b.ph;
    int done = 0;
    int64_t trackTravel = 0;
    for (int i = 0; i < 2000 && done < after; i++) {
        int32_t a0 = b.a;
        uint8_t ev = step(b);
        s.steps++;
        s.hash = hashBall(b, s.hash, ev);
        if (ev & EV_FLICK) { s.flicks++; s.flick = b.t; }
        if (ev & EV_LEAVE) { s.leaves++; s.leave = b.t; }
        if (ev & EV_ROLL) s.rolls++;
        if (ev & EV_DEFLECT) { CHECK(b.defl < 8); }
        if (ev & EV_FRET) { s.frets++; if (b.vz == 96) s.backs++; else s.lastHop = b.t - (s.flick > 0 ? s.flick : 0); }
        if (b.ph == ROTOR) {
            int32_t f = (b.psi & 0xFFFF) - 0x8000;
            bool rest = !b.z && !b.vz && (b.v < 2000 && b.v > -2000) && f < 0x1800 && f > -0x1800;
            s.idle = rest ? s.idle + 1 : 0;
        }
        if (ev & EV_BOUNCE) s.bounces++;
        if (ev & EV_LAND) { s.lands++; s.land = b.t; s.pocket = b.pocket; }
        if (b.ph == TRACK || (ev & EV_LEAVE)) trackTravel += (a0 - b.a + T) % T;
        if (b.ph != prevPh) {
            if (b.ph == ROTOR) {
                s.rotor = b.t;
                s.clampHit = b.v <= -0xF000;
            }
            if (b.ph == SETTLE) s.settle = b.t;
        }
        if (b.ph >= ROTOR) {
            int32_t av = b.v < 0 ? -b.v : b.v;
            if (av > s.maxRel) s.maxRel = av;
            if (prevPh >= ROTOR) {
                int32_t d = pocketDelta(b.psi - prevPsi, T);
                if (d < 0) d = -d;
                if (d > s.maxRel) s.maxRel = d;
            }
            prevPsi = b.psi;
        }
        if (b.rw > s.maxRotorW) s.maxRotorW = b.rw;
        if (trace) {
            fprintf(trace, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", b.t, b.ph, screenAngle(b),
                    rotorAngle(b), b.a, b.rho, b.r, b.z, ev, b.defl, b.pocket, b.w, b.v, b.n);
        }
        prevPh = b.ph;
        if (b.ph == DONE) done++;
    }
    s.hits = b.hits;
    s.trackTurnsQ16 = (int)(trackTravel / b.n);
    return s;
}

// Plan and launch the way the presenter does: begin, solve in slices, launch.
static int planSlices = 0, planMaxSteps = 0;
static Spin spinTo(uint8_t target, uint32_t seed, uint8_t n, bool quick, Ball *out = nullptr) {
    static Plan p;
    begin(p, target, seed, n, quick);
    int calls = 0;
    while (!solve(p, 25)) { if (++calls > 1000) { CHECK(!"solver never finishes"); break; } }
    calls++;
    if (calls > planSlices) planSlices = calls;
    if (p.dry.t > planMaxSteps) planMaxSteps = p.dry.t;
    CHECK(p.dry.t <= MAX_STEPS);
    Ball live;
    launch(p, live);
    Spin s = run(live);
    if (out) *out = live;
    return s;
}

// ---------------------------------------------------------------------------
struct Hist {
    int lo, hi, bin; int c[64]; int n, mn, mx; long sum;
    void init(int l, int h, int b) { lo = l; hi = h; bin = b; memset(c, 0, sizeof c); n = 0; mn = 1 << 30; mx = -(1 << 30); sum = 0; }
    void add(int v) {
        int k = (v - lo) / bin; if (v < lo) k = 0; if (k > 63) k = 63;
        c[k]++; n++; sum += v; if (v < mn) mn = v; if (v > mx) mx = v;
    }
    void print(const char *name, double scale = 1.0, const char *unit = "") {
        printf("  %s: n=%d min %.2f%s mean %.2f%s max %.2f%s\n", name, n, mn * scale, unit,
               n ? (double)sum / n * scale : 0.0, unit, mx * scale, unit);
        int peak = 1; for (int k = 0; k < 64; k++) if (c[k] > peak) peak = c[k];
        for (int k = 0; k < 64; k++) {
            if (!c[k]) continue;
            int bar = (c[k] * 50 + peak - 1) / peak;
            printf("    %7.2f-%-7.2f %6d ", (lo + k * bin) * scale, (lo + (k + 1) * bin - 1) * scale, c[k]);
            for (int j = 0; j < bar; j++) putchar('#');
            putchar('\n');
        }
    }
};

static void testLandingAllTargets() {
    for (int quick = 0; quick < 2; quick++)
        for (int n = 37; n <= 38; n++)
            for (uint32_t seed = 1; seed <= 200; seed++)
                for (int target = 0; target < n; target++) {
                    Spin s = spinTo((uint8_t)target, seed * 2654435761u, (uint8_t)n, quick);
                    CHECK_EQ(s.lands, 1);
                    if (s.pocket != target) { CHECK_EQ(s.pocket, target); printf("  seed %u n %d quick %d\n", seed, n, quick); }
                }
}

// 10,000 random (seed, target, wheel, pace): landing, timing, rotor speed.
static Hist hDur[2], hTrack[2], hDrop[2], hRot[2], hSet[2], hLaps[2], hFrets[2], hHits[2], hRel[2], hBacks[2], hIdle[2], hLastHop[2];
static void testRandom(int spins, bool checksOn) {
    for (int q = 0; q < 2; q++) {
        hDur[q].init(q ? 100 : 190, 0, 3); hTrack[q].init(q ? 40 : 90, 0, 4); hDrop[q].init(0, 0, 2);
        hRot[q].init(0, 0, 4); hSet[q].init(0, 0, 1); hLaps[q].init(0, 0, 4096); hFrets[q].init(0, 0, 1);
        hHits[q].init(0, 0, 1); hRel[q].init(0, 0, 4096); hBacks[q].init(0, 0, 1); hIdle[q].init(0, 0, 2); hLastHop[q].init(q ? 80 : 150, 0, 4);
    }
    int clamps[2] = {0, 0};
    for (int i = 0; i < spins; i++) {
        uint32_t seed = rand32();
        uint8_t n = (rand32() & 1) ? 38 : 37;
        bool quick = rand32() & 1;
        uint8_t target = (uint8_t)(rand32() % n);
        Spin s = spinTo(target, seed, n, quick);
        int dur = s.land - s.flick;
        if (checksOn) {
            CHECK_EQ(s.pocket, target);
            CHECK_EQ(s.flicks, 1); CHECK_EQ(s.lands, 1); CHECK_EQ(s.leaves, 1);
            CHECK_EQ(s.flick, WAIT_TICKS[quick]);
            CHECK(s.hits <= 2);
            if (dur < FLICK_TO_LAND_MIN[quick] || dur > FLICK_TO_LAND_MAX[quick]) {
                CHECK(!"flick-to-land outside the pace window");
                printf("  seed %u n %d quick %d: %d ticks\n", seed, n, quick, dur);
            }
            CHECK(s.maxRel < 0x10000);
        }
        clamps[quick] += s.clampHit;
        hDur[quick].add(dur);
        hTrack[quick].add(s.leave - s.flick);
        hDrop[quick].add(s.rotor - s.leave);
        hRot[quick].add(s.settle - s.rotor);
        hSet[quick].add(s.land - s.settle);
        hLaps[quick].add(s.trackTurnsQ16);
        hFrets[quick].add(s.frets);
        hHits[quick].add(s.hits);
        hBacks[quick].add(s.backs);
        hIdle[quick].add(s.idle);
        hLastHop[quick].add(s.lastHop);
        hRel[quick].add(s.maxRel);
    }
    for (int q = 0; q < 2; q++) {
        printf("%s (%d spins)\n", q ? "QUICK" : "FUN", hDur[q].n);
        hDur[q].print("flick -> land, s", 1.0 / 60, "s");
        hTrack[q].print("TRACK ticks");
        hDrop[q].print("DROP ticks");
        hRot[q].print("ROTOR ticks");
        hSet[q].print("SETTLE ticks");
        hLaps[q].print("laps on the track", 1.0 / 65536);
        hFrets[q].print("fret clicks");
        hBacks[q].print("bounce-backs");
        hIdle[q].print("ticks at rest before SETTLE (waiting for the earliest settle)");
        hLastHop[q].print("last fret hop, ticks after the flick");
        hHits[q].print("deflector hits");
        hRel[q].print("max |rotor-relative speed|, pockets/tick", 1.0 / 65536);
        printf("  entry speed capped: %d\n", clamps[q]);
        long tot = hTrack[q].sum + hDrop[q].sum + hRot[q].sum + hSet[q].sum;
        int share[4] = {(int)(hTrack[q].sum * 100 / tot), (int)(hDrop[q].sum * 100 / tot),
                        (int)(hRot[q].sum * 100 / tot), (int)(hSet[q].sum * 100 / tot)};
        printf("  phase split: TRACK %d%%  DROP %d%%  ROTOR %d%%  SETTLE %d%%\n", share[0], share[1], share[2], share[3]);
        if (checksOn)
            for (int k = 0; k < 4; k++) { CHECK(share[k] >= PHASE_SHARE_LO[k]); CHECK(share[k] <= PHASE_SHARE_HI[k]); }
    }
}

// Same seed twice: the same trace, tick for tick.
static void testDeterminism() {
    for (uint32_t seed = 1; seed < 300; seed++) {
        for (int q = 0; q < 2; q++) {
            Ball a, b;
            init(a, seed, 37 + (seed & 1), q, 0);
            init(b, seed, 37 + (seed & 1), q, 0);
            Spin sa = run(a), sb = run(b);
            CHECK_EQ(sa.hash, sb.hash);
            Ball c, d;                                  // and through the solver
            spinTo((uint8_t)(seed % 37), seed, 37 + (seed & 1), q, &c);
            spinTo((uint8_t)(seed % 37), seed, 37 + (seed & 1), q, &d);
            CHECK(memcmp(&c, &d, sizeof c) == 0);
        }
    }
}

// The rotor shift: identical up to the rotor, then exactly k pockets earlier.
static void testShiftInvariance() {
    for (uint32_t seed = 1; seed < 200; seed++) {
        uint8_t n = 37 + (seed % 2);
        bool q = (seed / 2) & 1;
        Ball b0, bk;
        uint32_t k = seed * 7 % n;
        init(b0, seed, n, q, 0);
        init(bk, seed, n, q, k << 16);
        const int32_t T = (int32_t)n << 16;
        for (int i = 0; i < 1000 && b0.ph != DONE; i++) {
            uint8_t e0 = step(b0), ek = step(bk);
            CHECK_EQ(e0, ek);
            CHECK_EQ(b0.ph, bk.ph);
            CHECK_EQ(b0.rho, (int32_t)((bk.rho + T - (int32_t)(k << 16)) % T));
            if (b0.ph < ROTOR) { CHECK_EQ(b0.a, bk.a); CHECK_EQ(b0.r, bk.r); CHECK_EQ(b0.z, bk.z); }
            else { CHECK_EQ(b0.psi & 0xFFFF, bk.psi & 0xFFFF); CHECK_EQ(b0.a, bk.a); }
        }
        step(bk);
        CHECK_EQ(bk.ph, DONE);
        CHECK_EQ((b0.pocket + n - bk.pocket) % n, (int)k);
    }
}

// Events and bounds tick by tick on a sample of spins.
static void testEventsAndBounds() {
    for (uint32_t seed = 1; seed <= 400; seed++) {
        uint8_t n = 37 + (seed & 1);
        bool q = seed & 2;
        Ball b;
        init(b, seed * 977, n, q, 0);
        const int32_t T = (int32_t)n << 16;
        int lastPh = WAIT, landT = -1;
        uint32_t travelled = 0, rolls = 0;
        for (int i = 0; i < MAX_STEPS + 50; i++) {
            int32_t a0 = b.a;
            uint8_t ph0 = b.ph;
            uint8_t ev = step(b);
            CHECK(b.ph >= lastPh);                      // phases only go forward
            CHECK(b.ph <= lastPh + 1);
            lastPh = b.ph;
            CHECK(b.a >= 0 && b.a < T);
            CHECK(b.rho >= 0 && b.rho < T);
            CHECK(b.rw > 0);
            CHECK(screenAngle(b) == (uint16_t)(b.a / n));
            CHECK(b.z >= 0);
            if (b.ph >= ROTOR) CHECK(b.psi >= 0 && b.psi < T);
            if (ph0 == TRACK || ph0 == DROP) {
                CHECK(b.w < 0);                         // counter-clockwise
                int32_t d = a0 - b.a; if (d < 0) d += T;
                travelled += d;
                if (ph0 == TRACK) CHECK(b.r <= 52 << 8 && b.r >= (52 << 8) - 450);   // wobble: ~1.7 px at most
            }
            if (ev & EV_ROLL) rolls++;
            if (ev & EV_DEFLECT) CHECK(b.ph == DROP);
            if (ev & EV_FRET) CHECK(b.ph == ROTOR);
            if (ev & EV_LAND) { landT = b.t; CHECK(b.pocket < n); CHECK_EQ(b.pocket, b.psi >> 16); }
            if (b.ph == DONE) break;
        }
        CHECK(landT > 0);
        // One EV_ROLL per sixteenth crossed, give or take the partial ones.
        int32_t sixteenths = (int32_t)(travelled / (uint32_t)(n << 12));
        CHECK((int32_t)rolls >= sixteenths - 1 && (int32_t)rolls <= sixteenths + 1);
        // Settled: the ball sits mid-pocket and rides the rotor.
        int32_t f = b.psi & 0xFFFF;
        CHECK(f > 0x8000 - 0x1000 && f < 0x8000 + 0x1000);
        for (int i = 0; i < 30; i++) {
            int32_t psi = b.psi;
            uint8_t ev = step(b);
            CHECK_EQ(ev, 0);
            CHECK_EQ(b.psi, psi);
            CHECK_EQ(b.a, (b.rho + b.psi) % T);
        }
    }
}

// A dry run never takes more than MAX_STEPS steps.
static void testBound() {
    int worst = 0;
    for (uint32_t seed = 0; seed < 20000; seed++) {
        Ball b;
        init(b, seed * 2246822519u + 7, 37 + (seed & 1), (seed >> 1) & 1, 0);
        int k = 0;
        while (b.ph != DONE && k < 5000) { step(b); k++; }
        CHECK(k <= MAX_STEPS);
        if (k > worst) worst = k;
    }
    printf("dry run: worst %d steps (MAX_STEPS %d); solve(25) needs at most %d calls\n", worst, MAX_STEPS, planSlices);
}

static int traceMode(int argc, char **argv) {
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 1;
    uint8_t n = argc > 3 ? (uint8_t)atoi(argv[3]) : 37;
    bool quick = argc > 4 ? atoi(argv[4]) != 0 : false;
    uint8_t target = argc > 5 ? (uint8_t)atoi(argv[5]) : 0;
    Plan p;
    begin(p, target, seed, n, quick);
    while (!solve(p, 25)) {}
    Ball live;
    launch(p, live);
    printf("# t ph screenAngle rotorAngle a rho r z ev defl pocket w v n\n");
    Ball first = live;
    fprintf(stdout, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", first.t, first.ph, screenAngle(first),
            rotorAngle(first), first.a, first.rho, first.r, first.z, 0, first.defl, first.pocket, first.w, first.v, first.n);
    run(live, stdout, 90);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--trace")) return traceMode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--stats")) {
        testRandom(argc > 2 ? atoi(argv[2]) : 4000, false);
        return 0;
    }
    testLandingAllTargets();
    printf("landing: 200 seeds x every target x EU/US x FUN/QUICK done (%d checks, %d failures)\n", checks, fails);
    testRandom(10000, true);
    testDeterminism();
    testShiftInvariance();
    testEventsAndBounds();
    testBound();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
