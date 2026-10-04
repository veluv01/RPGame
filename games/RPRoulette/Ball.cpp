// The roulette ball: docs/design/wheel.md section 3. PACE below catches the
// ball in a pocket 3.8 to 4.7 s after the start on FUN, 2.2 to 2.7 s on QUICK
// (quicker than the critique's 8.4(b), which asked for about 5 s and 3 s).
//
// One step() for the live ball and the solver's dry run. It reads only the
// Ball: its own generator, and the pace and n captured at init. Before the
// rotor phase nothing reads rho. On the rotor only psi's place within a
// pocket (its low 16 bits) and pocket *changes* matter, so starting the
// rotor k pockets further on lands the ball exactly k pockets earlier.
//
//   WAIT    the glove reaches; the ball waits at the flick point
//   TRACK   counter-clockwise at R52, slowing linearly to wDrop; below
//           wWob it wobbles inward (up to ~1.7 px)
//   DROP    pulled inward down the apron; w r^2 is kept, so it speeds up;
//           crossing a deflector (odd sixteenths) at R45..51 may hit it
//           (5/8, at most 2): a hop, 1/2..3/4 of the speed, radial stop
//   ROTOR   rotor frame. Down the number ring to R30, then the pockets:
//           a fret met low enough is hopped (likelier the faster) or
//           bounced off (v * -3/8); each pocket is a dip that pulls to its
//           middle. Caught when it can no longer climb a fret.
//   SETTLE  eases to the pocket's middle; EV_LAND when it gets there
//   DONE    rides the rotor
// Durations are kept in the pace window by the clock: no settle before
// minT, no hops after lateT, settle forced at maxT (ticks from the start).
#pragma GCC optimize("Os")
#include "Ball.h"

namespace ball {

namespace {

constexpr int32_t ONE = 1 << 16;                  // one pocket
constexpr int32_t VMAX = 0xF000;                  // rotor-relative speed cap (< 1 pocket a tick)
// Radii in Q8 px (tools/wheel.py: track R52, deflectors R48 on the apron,
// rotor lip R44, frets from the separator inward, ball at rest R30).
constexpr int32_t R_TRACK = 52 << 8, R_DEFL_LO = 45 << 8, R_DEFL_HI = 51 << 8,
                  R_ROTOR = 44 << 8, R_FRETS = 35 << 8, R_REST = 30 << 8;
constexpr int32_t A_FLICK = 176 << 8;             // turn-Q16: under the croupier's glove
constexpr int32_t Z_DEFL = 512, Z_FRET = 320;     // a ball higher than this flies over (Q8 px)

// Speeds in turn-Q16 per tick (65,536 = one turn; 1 rev/s = 1,092), scaled
// by n at init, which is exact. Q8 px for radii and heights.
struct Pace {
    uint8_t wait, settle;           // ticks
    uint8_t tTrack, tTrackRnd;      // time on the track: tTrack + rnd % tTrackRnd
    uint16_t rotorW;                // rotor speed, + rnd % 32
    uint16_t w0;                    // flick speed, + rnd % 256
    uint16_t wWob, wDrop;           // wobbles below wWob, leaves the track at wDrop
    uint8_t pull, grav;             // apron pull (Q8 px/tick^2), gravity for hops
    uint8_t fric, dip;              // in a pocket: v -= v >> fric, and the dip's pull f >> dip
    uint8_t ring, keep;             // number ring: r eases by >> ring; a hop keeps keep/16 of v
    uint16_t minT, lateT, maxT;     // ticks from the start: earliest settle, last hop, forced settle
};
constexpr Pace PACE[2] = {
    // wait settle track     rotor flick wobble drop pull grav fric dip ring keep  settle window
    {24, 12, 124, 16, 476, 2500, 1300, 800, 8, 55, 5, 4, 4, 15, 24 + 205, 24 + 244, 24 + 260},  // FUN
    {14, 7, 68, 10, 530, 2450, 1300, 730, 16, 90, 4, 3, 3, 14, 14 + 116, 14 + 136, 14 + 150},   // QUICK
};
static_assert(PACE[0].w0 > PACE[0].wDrop && PACE[1].w0 > PACE[1].wDrop, "the track ends");
static_assert(PACE[0].pull > 0 && PACE[1].pull > 0, "the drop ends");
static_assert(PACE[0].maxT + PACE[0].settle <= MAX_STEPS && PACE[1].maxT + PACE[1].settle <= MAX_STEPS,
              "a dry run fits MAX_STEPS");

uint32_t rnd(Ball &b) {
    uint32_t x = b.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return b.rng = x;
}

int32_t iabs(int32_t x) { return x < 0 ? -x : x; }

// Hop height under gravity; a hard landing bounces at 3/8.
uint8_t fall(Ball &b, int32_t g) {
    if (!b.z && !b.vz) return 0;
    b.vz -= g;
    int32_t z = b.z + b.vz;
    if (z > 0) { b.z = z; return 0; }
    b.z = 0;
    if (b.vz < -100) { b.vz = (-b.vz * 3) >> 3; return EV_BOUNCE; }
    b.vz = 0;
    return 0;
}

}  // namespace

void init(Ball &b, uint32_t seed, uint8_t n, bool quick, uint32_t rotorShift) {
    const Pace &p = PACE[quick];
    b = Ball();
    b.n = n;
    b.quick = quick;
    uint32_t x = (seed + 0x9E3779B9u) * 0x85EBCA6Bu;
    b.rng = (x ^ (x >> 15)) | 1;
    const uint32_t T = (uint32_t)n << 16;
    b.rho = (int32_t)((rnd(b) % T + rotorShift) % T);
    x = rnd(b);
    b.rw = (int32_t)(p.rotorW + (x & 31)) * n;
    b.v = (int32_t)(p.w0 + ((x >> 5) & 255)) * n;          // the flick, held until WAIT ends
    b.dec = (b.v - (int32_t)p.wDrop * n) / (int32_t)(p.tTrack + (x >> 13) % p.tTrackRnd);
    b.a = A_FLICK * n;
    b.r = R_TRACK;
}

uint8_t step(Ball &b) {
    const Pace &p = PACE[b.quick];
    const int32_t n = b.n, T = n << 16;
    const uint8_t ph = b.ph;
    uint8_t ev = 0;
    uint16_t t = ++b.t, pt = ++b.phaseT;
    int32_t rho = b.rho + b.rw;                             // clockwise, slowly slowing
    if (rho >= T) rho -= T;
    b.rho = rho;
    b.rw -= b.rw >> 11;
    if (ph >= DROP) ev = fall(b, p.grav);                   // hops, on the apron and the rotor
    if (ph == WAIT) {
        if (pt >= p.wait) {
            b.w = -b.v;                                     // counter-clockwise
            b.v = 0;
            b.ph = TRACK;
            ev = EV_FLICK;
        }
    } else if (ph < ROTOR) {                                // TRACK, DROP
        const int32_t sixteenth = n << 12;
        int32_t sec = b.a / sixteenth, a = b.a + b.w;       // w < 0 here
        if (a < 0) a += T;
        b.a = a;
        bool crossed = a / sixteenth != sec;                // sec: the edge just crossed
        if (crossed) ev |= EV_ROLL;
        if (ph == TRACK) {
            b.w += b.dec;
            int32_t s = -b.w, wob = p.wWob * n;
            if (s < wob) {                                  // losing the wall: wobble inward
                int32_t k = pt % 10, tri = k < 5 ? k : 10 - k;
                b.r = R_TRACK - (((wob - s) * tri) >> 8);
            }
            if (s <= (int32_t)p.wDrop * n) { b.ph = DROP; ev |= EV_LEAVE; }
        } else {
            b.vr -= p.pull;
            b.w -= 2 * b.w * b.vr / b.r;                    // angular momentum: w r^2 kept
            b.w -= b.w >> 7;                                // apron friction
            int32_t r = b.r += b.vr;
            uint32_t x;
            if (crossed && (sec & 1) && (uint32_t)(r - R_DEFL_LO) <= (uint32_t)(R_DEFL_HI - R_DEFL_LO) &&
                b.z < Z_DEFL && b.hits < 2 && ((x = rnd(b)) & 7) < 5) {
                b.hits++;
                b.defl = (uint8_t)(sec >> 1);               // deflector k at (2k+1)/16 turn
                ev |= EV_DEFLECT;
                b.vz = 240 + ((x >> 3) & 127);
                b.w = (b.w >> 3) * (int32_t)(4 + (x >> 10) % 3);
                b.vr = 0;                                   // stopped dead, then falls again
            }
            if (r <= R_ROTOR) {
                b.ph = ROTOR;
                int32_t psi = a - rho;
                b.psi = psi < 0 ? psi + T : psi;
                int32_t v = b.w - b.rw;
                b.v = v < -VMAX ? -VMAX : v;
            }
        }
    } else if (ph < DONE) {                                 // ROTOR, SETTLE
        b.vr += (((R_REST - b.r) >> p.ring) - b.vr) >> 2;  // down the number ring, easing in
        int32_t r = b.r += b.vr;
        int32_t f = (b.psi & 0xFFFF) - 0x8000;              // from the pocket's middle
        if (ph == SETTLE) {                                 // ease to the middle
            int32_t lo = (int32_t)b.pocket << 16, psi;
            b.v = (b.v - (f >> 2)) >> 1;
            psi = b.psi + b.v;
            if (psi < lo) psi = lo;
            if (psi > lo + 0xFFFF) psi = lo + 0xFFFF;
            if (pt >= p.settle) {
                psi = lo + 0x8000;
                b.v = 0;
                b.ph = DONE;
                ev |= EV_LAND;
            }
            b.psi = psi;
        } else {
            // Too little energy to climb a fret (or out of time): it is caught.
            int32_t vq = b.v >> 6, fq = f >> 6;
            bool caught = t >= p.lateT || (vq * vq << p.dip) + fq * fq < 512 * 512;
            bool low = b.z < Z_FRET && r < R_FRETS;         // down among the frets
            if (b.z || r >= R_FRETS) b.v -= b.v >> 7;
            else b.v -= (b.v >> (caught ? 2 : p.fric)) + (f >> p.dip);
            int32_t psi = b.psi + b.v;
            if ((uint32_t)(psi - (b.psi & ~0xFFFF)) >= (uint32_t)ONE && low) {
                ev |= EV_FRET;
                int32_t av = iabs(b.v);
                uint32_t x = rnd(b);
                if (!caught && av > 5243 + (int32_t)(x & 0x3FFF)) {     // over it: likelier the faster
                    int32_t vz = 160 + (av >> 7) + (int32_t)(x >> 27) - pt;   // lower as it tires
                    b.vz = vz > 480 ? 480 : vz;
                    b.v = (b.v * p.keep) >> 4;
                } else {                                    // back off it
                    b.v = (-b.v * 3) >> 3;
                    b.vz = 96;
                    psi = b.psi;
                }
            }
            if (psi < 0) psi += T;
            if (psi >= T) psi -= T;
            b.psi = psi;
            if ((caught && !(b.z | b.vz) && r < R_FRETS && t >= p.minT) || t >= p.maxT) {
                b.pocket = (uint8_t)(psi >> 16);            // decided: it cannot leave now
                b.ph = SETTLE;
            }
        }
    }
    if (b.ph != ph) b.phaseT = 0;
    if (b.ph >= ROTOR) {
        int32_t a = rho + b.psi;
        b.a = a >= T ? a - T : a;
    }
    return ev;
}

void begin(Plan &p, uint8_t targetIndex, uint32_t seed, uint8_t n, bool quick) {
    init(p.dry, seed, n, quick, 0);
    p.seed = seed;
    p.target = targetIndex;
}

bool solve(Plan &p, uint16_t maxSteps) {
    while (p.dry.ph != DONE && maxSteps--) step(p.dry);
    return p.dry.ph == DONE;
}

void launch(Plan &p, Ball &live) {
    solve(p, MAX_STEPS);                                    // normally done already; bounded
    uint8_t n = p.dry.n;
    init(live, p.seed, n, p.dry.quick, (uint32_t)((p.dry.pocket + n - p.target) % n) << 16);
}

uint16_t screenAngle(const Ball &b) { return (uint16_t)(b.a / b.n); }
uint16_t rotorAngle(const Ball &b) { return (uint16_t)(b.rho / b.n); }

}  // namespace ball
