// Two real 3D dice: integer rigid-ish body physics and a pinhole camera.
//
// World units: a die's edge is 16; X right, Y up (the felt is Y = 0), Z away
// from the shooter towards the back wall. Positions and velocities are Q8
// (x256), one physics step per 60 Hz logic tick. Orientation is three Euler
// angles (65536 a turn): R = Ry(yaw) Rx(pitch) Rz(roll). A die at rest has
// pitch and roll on quarter turns, so exactly one face points up.
//
// The roll's result never comes from the physics. The rules roll the dice
// (Craps.cpp); the presenter then throws, simulates the whole
// tumble ahead (it is deterministic), sees which face of each die lands on
// top and paints the pips so that face shows the rolled number - a real die
// layout, opposite faces adding to 7. Then the same tumble plays out live.
//
// Graphics-free apart from draw(), so the physics is host-tested.
#pragma once
#include <stdint.h>

namespace d3 {

const int EDGE = 16, HALF = 8;          // die size, world units
const int32_t WALL_Z = 132 << 8;        // the back wall's face
const int32_t NEAR_Z = -24 * 256;       // the near rail
const int32_t SIDE_X = 58 << 8;         // the side rails (+-)

enum State : uint8_t { HELD, AIR, SETTLE, REST };

struct Die {
    int32_t  x, y, z;                   // centre, Q8
    int32_t  vx, vy, vz;                // Q8 per tick
    uint16_t yaw, pitch, roll;          // 65536 = a turn
    int16_t  wyaw, wpitch, wroll;       // per tick
    uint8_t  label[6];                  // pips on local faces +X -X +Y -Y +Z -Z
    uint8_t  state, settleT;
    uint32_t seed;                      // the die's own bounce noise (deterministic)
};

struct Pair {
    Die      d[2];
    uint16_t t;                         // ticks since the throw
    uint8_t  hits;                      // events since last asked (bounce/wall bits)
    uint8_t  paint[2][6];               // the result's pips, put on at the back wall
    bool     painting;
};

enum Hit : uint8_t { HIT_FLOOR = 1, HIT_WALL = 2, HIT_DICE = 4, HIT_SIDE = 8 };

// Q8 rotation matrix: m[row][col], columns = the die's local X, Y, Z axes in
// the world.
struct Mat { int16_t m[3][3]; };
Mat matrix(const Die &d);
uint8_t upFace(const Die &d);           // local face index (0..5) pointing up

// In the hand: both dice held near the camera, jiggled by the caller.
void hold(Pair &p);
// Let go. power 0..255 (how hard), spin is presentation randomness: it only
// changes how the dice tumble, never what they show.
void release(Pair &p, uint8_t power, uint32_t spin);
void step(Pair &p);                     // one 60 Hz tick
bool atRest(const Pair &p);
// Simulate a copy to rest, then work out how to repaint each die so it ends
// showing a and b (changing as few of its pips as it can). The new pips go
// on when the dice hit the back wall - small, spinning, in a burst of sparks.
void fix(Pair &p, uint8_t a, uint8_t b);
// A standard die (opposites sum to 7, right-handed) with v on face `up`;
// spin 0..3 picks which way the rest turn.
void labelDie(Die &d, uint8_t up, uint8_t v, uint8_t spin);

// The camera: a pinhole at (camX, camY, camZ) looking along +Z, tipped down
// by `pitch`, with the principal point at (sx0, sy0) on screen. Throwing,
// it looks level with the principal point above the screen (an off-axis
// lens: the table runs away to the wall, verticals stay upright); when the
// dice stop it cranes up and looks down on them.
struct Cam {
    int32_t camX, camY, camZ;           // Q8 world
    uint8_t pitch;                      // 1/256 turn, down
    int16_t focal;                      // px at unit depth per world unit
    int16_t sx0, sy0;                   // px
};
void defaultCam(Cam &c);
// World point (Q8) -> screen, 1/16 px. Returns false if behind the camera.
bool project(const Cam &c, int32_t x, int32_t y, int32_t z, int16_t &sx, int16_t &sy);
int16_t scaleAt(const Cam &c, int32_t y, int32_t z);   // px per world unit there, x16

// Draw a die (and nothing else); colours: face light, mid, dark, pip, edge.
struct Look { uint8_t light, mid, dark, pip, pipDark, edge; };
void draw(const Die &d, const Cam &c, const Look &l);
void shadow(const Die &d, const Cam &c, uint8_t colour);

}  // namespace d3
