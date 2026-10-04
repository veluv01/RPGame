// The action bar at the foot of the table: your choices as buttons that
// open out as the cursor reaches them (CHBlackjack's accordion).
#pragma once
#include <stdint.h>
#include "Table.h"

namespace bar {

void reset();
void draw(const Table &t, uint32_t frame);
bool easing();                       // buttons still opening or closing

}  // namespace bar
