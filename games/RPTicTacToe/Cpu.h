// The dealer's brain. 3x3 tables: a depth-limited search of the real rules.
// The big felts: every empty cell scored by the lines it could still make or
// break, a few cells a tick so the frame rate holds. Pure, host-tested.
#pragma once
#include "Rules.h"

namespace cpu {

struct Move { uint8_t cell, arg; };

// level: 0 TIPSY, 1 SHARP, 2 SHARK. Plays the side to move.
void begin(const Board &b, uint8_t level, uint32_t *rng);
bool step(const Board &b, Move &out);       // once a tick; true when decided
uint8_t bid(const Board &b, uint8_t level, uint32_t *rng);   // AUCTION: the dealer's bid
bool winsNow(const Board &b, uint8_t side);                  // 3x3 tables: a winning move exists

uint32_t rnd(uint32_t *rng);

}  // namespace cpu
