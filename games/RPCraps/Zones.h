// Every spot on the layout and in the bar, as data: where it is printed,
// which bet it takes, and where its chips sit (the anchor, which is also
// where the cursor points). Two layouts - Classic and Beginner - share one
// renderer (Felt.cpp) and one navigation.
//
// Graphics-free, so the host tests check that every spot can be reached.
#pragma once
#include <stdint.h>

class Craps;

// Bar spots, after the bets: the four chips and ROLL.
enum : uint8_t { Z_CHIP0 = 0x80, Z_ROLL = 0x84 };

struct Zone {
    uint8_t x, y, w, h;         // printed area
    uint8_t bet;                // Bet, or Z_CHIP0 + i / Z_ROLL
    uint8_t ax, ay;             // chip anchor and cursor point
};

namespace zones {

uint8_t count(uint8_t table);
const Zone &at(uint8_t table, uint8_t i);
uint8_t find(uint8_t table, uint8_t bet);       // index, or 0xFF
bool    usable(const Craps &g, uint8_t i);      // the cursor may stop here
bool    inBar(uint8_t table, uint8_t i);
// Where spot `bet`'s chips are drawn (come points included).
bool    anchor(uint8_t table, uint8_t bet, int &x, int &y);
// The usable spot nearest `from` in screen direction (ux, uy) - preferring
// ones straight ahead, wrapping round to the farthest the other way when
// there is none (CHChess's cursor). (0, 0): simply the nearest.
uint8_t nearest(const Craps &g, uint8_t from, int ux, int uy);

}  // namespace zones
