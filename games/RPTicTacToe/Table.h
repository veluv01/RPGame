// The wall band over the tables room: CHBlackjack's croupier under his lamp,
// the rail with his chip rack, and his speech bubble.
#pragma once
#include <stdint.h>

namespace table {

constexpr int WALL_H = 42, RAIL_Y = 42, RAIL_H = 4;
constexpr int DEALER_X = 2, DEALER_Y = 0;

enum : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

void wall();
void dealer(uint8_t expr, uint8_t look, bool alt, int x = DEALER_X, int y = DEALER_Y);
void rail();
void bubble(const char *text, int typed);     // typed: characters shown so far

}  // namespace table
