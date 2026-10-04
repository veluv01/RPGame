// Chips, the puck and little dice faces.
//
// The chips are CHBlackjack's ($1 white, $5 red, $25 green, $100 black) at
// full size in the bar and in flight, and a small 9x4 cut for stacks on the
// crowded craps layout.
#pragma once
#include <stdint.h>

namespace art {

const uint8_t DENOMS = 4;
extern const uint16_t CHIP_VALUE[DENOMS];

void chip(int cx, int y, uint8_t denom, bool top);          // 15x5
void chipSmall(int cx, int y, uint8_t denom, bool top);     // 9x4
int  chipDenom(int32_t amount);                             // largest chip that fits
// A stack of small chips with its base at baseY, at most maxChips tall,
// coloured largest denomination first.
void stackSmall(int cx, int baseY, int32_t amount, uint8_t maxChips = 3);
void stack(int cx, int baseY, int32_t amount, uint8_t maxChips);

// The pucks, round: ON a white badge (11 px) showing the point number
// (number 0: "ON"); OFF black (13 px).
void puck(int cx, int cy, bool on, uint8_t number = 0);

// A die face, size 5 (pips one pixel) or 7, red with white pips.
void dieFace(int x, int y, uint8_t size, uint8_t v);

}  // namespace art
