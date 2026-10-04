// Racing and bearing off. Once the two sides have passed each other nothing
// either does can touch the other, and the best play is simply the one that
// gets your own fifteen checkers off in the fewest rolls. A small table says
// how many rolls a side still needs, near enough; the CPU picks the play
// that leaves it the lowest count. (The network is not asked: when the game
// is a race its choices are too close for it to tell apart.)
//
// The table is fitted by tools/train/race.cpp to the exact answer for every
// home-board position, which is small enough to work out on a PC but not to
// carry here.
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace race {

constexpr int TABLE_SIZE = 25;
#ifdef RACE_HOST
#define RACE_CONST
#else
#define RACE_CONST const
#endif
// 1/16 rolls. Per home point: a first checker, a second, a third, then each
// one more; then a constant.
extern RACE_CONST uint8_t TABLE[TABLE_SIZE];

// Rolls `side` still needs to bear everything off, in 1/16ths (an estimate).
int16_t cost(const bg::Board &b, uint8_t side);

}  // namespace race
