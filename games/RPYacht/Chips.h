// Chips and flat dice faces.
//
// The chips are CHBlackjack's ($1 white, $5 red, $25 green, $100 black) at
// full size, and a small 9x4 cut.
#pragma once
#include <stdint.h>

namespace art {

const uint8_t DENOMS = 4;
extern const uint16_t CHIP_VALUE[DENOMS];

void chip(int cx, int y, uint8_t denom, bool top);          // 15x5
void chipSmall(int cx, int y, uint8_t denom, bool top);     // 9x4
int  chipDenom(int32_t amount);                             // largest chip that fits
// A stack of chips with its base at baseY, at most maxChips tall, coloured
// largest denomination first.
void stackSmall(int cx, int baseY, int32_t amount, uint8_t maxChips = 3);
void stack(int cx, int baseY, int32_t amount, uint8_t maxChips);

// The dice's colours by seat: red, blue, gold, white.
void dieColours(uint8_t player, uint8_t &body, uint8_t &shade, uint8_t &pip);
// A die face seen from above: size 5 (pips one pixel), 7 or 17 (the tray).
void dieFace(int x, int y, uint8_t size, uint8_t v, uint8_t player = 0);

}  // namespace art
