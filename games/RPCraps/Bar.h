// The bar along the bottom: the four chips (the one in hand lifted) and ROLL.
#pragma once
#include <stdint.h>

namespace bar {

// denom: the chip in hand; cursor: the bar slot under the cursor (0..3
// chips, 4 ROLL) or -1; rollOn: the shooter may roll; held: A is down on
// ROLL. Skips itself when nothing changed; true if it drew.
bool draw(uint8_t denom, int8_t cursor, bool rollOn, bool held, uint32_t frame);
void invalidate();

}  // namespace bar
