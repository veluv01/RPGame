// The ball and the rotor: integer physics, deterministic for a seed, and a
// solver that makes the ball land in the pocket the rules chose. Pure (no
// graphics, no sound): the presenter draws it and plays its events, and the
// host tests run it. docs/design/wheel.md section 3.
//
// Angles are in pocket units: one turn = n << 16 (n = 37 or 38 pockets),
// clockwise on screen from +x, so turning the rotor by whole pockets
// (k << 16) moves the landing pocket by exactly k - which is what the
// solver does: it runs the spin once with the rotor at 0, sees where the
// ball lands, and starts the real spin with the rotor turned to make that
// the target. Nothing the ball does before it reaches the rotor depends on
// the rotor, and on the rotor only its phase within a pocket matters.
#pragma once
#include <stdint.h>

namespace ball {

enum Phase : uint8_t {
    WAIT,        // the rotor turns; the croupier's glove reaches for the ball
    TRACK,       // flicked: round the track against the rotor, slowing
    DROP,        // off the track, down the apron, past the deflectors
    ROTOR,       // on the rotor: hopping and rattling over the frets
    SETTLE,      // in a pocket, easing to its middle
    DONE,        // riding the rotor
};

// step()'s events, for sound and sparkle (several may come at once).
enum : uint8_t {
    EV_FLICK = 1,       // launched
    EV_ROLL = 2,        // another 1/16 turn round the track or the apron (a soft tick)
    EV_LEAVE = 4,       // left the track
    EV_DEFLECT = 8,     // hit a deflector (defl = which)
    EV_FRET = 16,       // clicked over a fret
    EV_BOUNCE = 32,     // hit the pocket floor
    EV_LAND = 64,       // settled: pocket = where
};

struct Ball {
    int32_t a, w;               // the ball's angle and speed (world), pocket units
    int32_t rho, rw;            // the rotor's
    int32_t psi, v;             // on the rotor: angle relative to it, relative speed (v: the flick speed in WAIT)
    int32_t dec;                // the track's deceleration per tick
    int32_t r, vr, z, vz;       // radius, radial speed, hop height, vertical speed (Q8 px)
    uint32_t rng;               // its own generator (never fx::rnd)
    uint16_t t, phaseT;         // ticks since the start, and in this phase
    uint8_t ph, n, quick, hits, pocket, defl;   // pocket: decided from SETTLE on
};

// A spin from the start of the croupier's reach (WAIT). rotorShift: added
// to the rotor's starting angle (pocket units, a multiple of 1 << 16).
void init(Ball &b, uint32_t seed, uint8_t n, bool quick, uint32_t rotorShift);
uint8_t step(Ball &b);                  // one 60 Hz tick; returns events

// The solver, spread over ticks so no frame runs long: begin(), then
// solve() a slice at a time until it returns true, then launch().
struct Plan { Ball dry; uint32_t seed; uint8_t target; };   // dry holds n and the pace
constexpr uint16_t MAX_STEPS = 300;     // a whole spin, WAIT to EV_LAND, never takes more
void begin(Plan &p, uint8_t targetIndex, uint32_t seed, uint8_t n, bool quick);
bool solve(Plan &p, uint16_t maxSteps); // true when solved
void launch(Plan &p, Ball &live);       // the real spin, landing on the target (solves what is left)

// For drawing: the ball's angle on screen in 1/65536 turn, and the rotor's.
uint16_t screenAngle(const Ball &b);
uint16_t rotorAngle(const Ball &b);

}  // namespace ball
