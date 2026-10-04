// The CPU player: given a position and a roll, which play?
//
// It tries every play the roll allows and asks the network (Net.h) which
// position it would rather be in. The strongest opponent then looks a roll
// further for its best few: every roll the other side could throw next, and
// its best answer to each. In a pure race the count in Race.h decides.
//
// The work is done a slice at a time from the game's 60 Hz tick - step(n)
// evaluates up to n positions and returns - so frames keep coming without
// the search needing a stack or a hook of its own, and a scripted run takes
// the same number of ticks every time. Nothing here rolls dice: the roll is
// handed in, and the only randomness (the beginner's unsteady judgement)
// comes from a generator of the CPU's own.
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace ai {

enum Level : uint8_t { BEGINNER, EXPERT, GRANDMASTER, LEVELS };

struct Play {
    uint8_t n;                  // steps: 0 = no legal move
    uint8_t from[4], die[4];
};

void start(const bg::Board &b, uint8_t side, uint8_t d1, uint8_t d2, uint8_t level, bg::Rng &noise);
bool step(uint16_t positions);  // true when the choice is made
void chosen(Play &p);
// The coach and the hints: the score of the chosen play (as the first look
// scored it), and of any position after a play of the same roll (by the
// same yardstick: the network, or in a race the count); a race or not.
int32_t bestScore();
int32_t judge(const bg::Board &b);
bool racingNow();
constexpr int32_t RACE_ROLL = 16 * 64;      // a roll in a race, in score units
// While it thinks: the point (in the side's own numbering) the play it is
// weighing starts from, 0 if none - where its glove hovers.
uint8_t considering();
uint32_t positions();           // evaluated so far (the debug protocol reports it)

}  // namespace ai
