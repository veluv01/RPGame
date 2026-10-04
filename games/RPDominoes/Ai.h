// The CPU: no search worth the name, the rules of thumb a good player uses.
// It sees what a player sees - its own hand, the line, how many tiles the
// other side holds and which pips it has shown it lacks - never the other
// hand or the boneyard.
//
// ROOKIE plays any tile half the time. REGULAR takes the points on offer,
// then sheds its heaviest tiles, doubles first. SHARK also keeps its own next
// turn open, leaves ends the other side has passed on, and weighs what each
// tile it cannot see would score in reply.
#pragma once
#include <stdint.h>
#include "Dominoes.h"

namespace ai {

enum Level : uint8_t { ROOKIE, REGULAR, SHARK, LEVELS };
struct Move { uint8_t tile, arm; };

// The side's play; false if it has none.
bool choose(const dom::Round &r, uint8_t side, uint8_t level, dom::Rng &rng, Move &m);

}  // namespace ai
