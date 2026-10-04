// The CPU player: which column?
//
// A plain alpha-beta search over the bitboards in Rules.h, the middle
// columns tried first. Before it looks any deeper, every position is asked
// the three questions a player asks: can I win now, must I block, and which
// columns would hand the other side a win on top of my disc.
//
// The work is done a slice at a time from the game's 60 Hz tick - step(n)
// visits up to n positions and returns - with the search's stack kept here
// rather than on the machine's, so frames keep coming and a scripted run
// takes the same number of ticks every time (CHBackgammon's arrangement).
// The only randomness, the weaker opponents' unsteady judgement, comes from
// the generator handed in.
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace ai {

enum Level : uint8_t { ROOKIE, SHARK, BOSS, LEVELS };

// Scores are from the side to move's point of view. A win by the move at
// ply p (0 = the move being chosen) is WIN - p, a loss the same negated;
// anything in between is judgement.
constexpr int16_t WIN = 1000;
constexpr int16_t SURE = WIN - 64;
inline bool winning(int16_t s) { return s >= SURE; }
inline bool losing(int16_t s) { return s <= -SURE; }
// A forced win: how many of its own moves, this one included, until the four.
inline uint8_t movesToWin(int16_t s) { return (uint8_t)((WIN - s) / 2 + 1); }

void start(const c4::Board &b, uint8_t side, uint8_t level, c4::Rng &noise);
bool step(uint16_t positions);  // true when the choice is made
uint8_t chosen();               // the column
int16_t score();                // what the search made of the position (its best move, whatever it then chose)
uint8_t considering();          // while it thinks: the column it is weighing
uint32_t positions();           // visited so far (the debug protocol reports it)
uint8_t depthReached();

}  // namespace ai
