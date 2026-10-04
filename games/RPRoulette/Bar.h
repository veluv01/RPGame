// The action bar at the foot of the betting view: CLR, the five chips and
// SPIN (CHBlackjack's bet bar). The glove visits it like any other spot.
#pragma once
#include <stdint.h>

namespace bar {

// active: the chip in use (0..4); hover: the slot under the glove (0 CLR,
// 1..5 the chips, 6 SPIN; 0xFF none); armed: CLR waiting for a second press;
// canSpin: SPIN lit; ox: shifted sideways (the whip).
// Returns false if nothing changed since the last call (no redraw needed).
bool draw(uint8_t active, uint8_t hover, bool armed, bool canSpin, int ox, bool force);

}  // namespace bar
