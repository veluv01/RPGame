// Drawing the wheel: a tilted (2:1) bowl of stacked ellipses, the rotor
// painted from a polar angle map through a per-frame colour table, the cone
// and turret, and the ball. docs/design/wheel.md; tools/wheel.py draws the
// same pixels on the PC.
#pragma once
#include <stdint.h>

namespace wheelart {

struct BallView {
    uint32_t angle;         // pocket units (1 turn = n << 16), ball::Ball::a
    int16_t r, z;           // radius and hop height, Q8 px
    int32_t w;              // speed (pocket units a tick): the trail
    bool snap;              // settled: drawn on the rotor's step grid
};

// The wheel centred on (cx, cy) (cx even), drawn into columns [x0, x1)
// (even) and rows [y0, y1). rho: the rotor's angle (pocket units). hi: the
// wheel index lit in hiColour (0xFF none). flash: deflectors lit (bit k).
// The felt and the bowl - which never move - are painted only in rows
// [by0, by1) (all of them when the wheel comes into view, then just where
// the ball, the sparks or a banner were); the rotor, the cone, the turret
// and the ball are drawn every time. Uses RPGfx's chunk scratch (render
// time only).
void draw(int cx, int cy, uint32_t rho, uint8_t n, uint8_t hi, uint8_t hiColour,
          int x0, int x1, int y0, int y1, int by0, int by1, bool felt, const BallView *ball, uint8_t flash);

// The rows the ball (with its trail and hop shadow) covers.
void ballRows(int cx, int cy, const BallView &b, uint8_t n, int &lo, int &hi);

// Where the ball is on screen (its centre), for sparks and the eyes.
void ballXY(int cx, int cy, const BallView &b, uint8_t n, int &x, int &y);

}  // namespace wheelart
