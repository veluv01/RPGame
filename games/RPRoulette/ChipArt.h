// Chips: CHBlackjack's procedural chips ($1 white, $5 red, $10 blue, $25
// green, $100 black), and small ones for the betting layout's 9 px cells.
#pragma once
#include <stdint.h>

namespace art {

extern const int32_t CHIP_VALUE[5];
int chipDenom(int32_t amount);               // the largest chip <= amount (0..4)

// A chip seen at an angle, 15 px wide (cx-7..cx+7), rows y-1..y+4; top =
// draw its face (only the top chip of a stack shows one).
void chip(int cx, int y, uint8_t d, bool top);
void chipStack(int cx, int baseY, int32_t amount, uint8_t maxChips = 10);

// A layout stack, 7 px wide, centred on (x, y), its foot at y + 2: one row
// per chip (up to 4), coloured by the biggest chip in it. outline < 0 =
// its own (INK, GOLD for $100 so it shows on black cells).
void miniStack(int x, int y, int32_t amount, int outline = -1);
// Where a chip would land on an empty line or corner: a ring.
void ghost(int x, int y, uint8_t c);

}  // namespace art
