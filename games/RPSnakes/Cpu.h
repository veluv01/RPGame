// The CPU's one decision: in ARCADE, which of the two dice to move by.
// A pure function of the game's state (it never touches the dice).
#pragma once
#include <stdint.h>

namespace cpu {

// Player p (a CPU) has thrown d1 and d2: 0 or 1.
// EASY takes a win and otherwise picks blind; FAIR takes the square that is
// further on; SHARK takes the square with the fewest turns still to go,
// weighing the rivals it bumps and the chance of being bumped there itself.
uint8_t pick(uint8_t p, uint8_t d1, uint8_t d2);

extern const uint8_t TURNS[101];    // turns to go from each square, in eighths

}  // namespace cpu
