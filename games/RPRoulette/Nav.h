// Moving the glove over the layout: CHChess's nearest-spot-in-a-direction
// (nearest() in CHChess's Screens.cpp), banded so it keeps to the
// row or column it is in. Pure. docs/design/layout.md section 3.
//
// A tap visits every spot, lines and corners included (half a cell a
// press), and wraps round to the farthest spot the other way when there is
// nothing ahead. A held direction (auto-repeat) runs whole cells - numbers,
// outside bets and the bar - and stops at the edge.
#pragma once
#include <stdint.h>

namespace nav {

// The glove: the spot it is on, and its x (inside a wide cell - a dozen or
// an even-money bet - the glove keeps the x it came in with, so going up
// and down again comes back to the same number).
struct Glove { uint8_t spot; uint8_t x, y; };

Glove at(uint8_t spot, bool us);            // the glove on a spot, at its anchor
// One step in direction (dx, dy) (each -1..1, screen axes: dy +1 = down).
// Returns false, leaving g alone, if there is nowhere to go.
bool step(Glove &g, int8_t dx, int8_t dy, bool tap, bool us);

}  // namespace nav
