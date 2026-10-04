// The CPU: it reads the whole flash word list every turn and tries each word
// everywhere it could go.
//
// The list is stored in the densest form that could be found, which cannot
// be walked letter by letter the way a word graph can; but it can be read
// from end to end in a fraction of a second, so the search is turned
// around: for every word, where on the board does it fit? A word that needs
// letters the rack has not got must run through one of them on the board,
// which leaves few places to try.
//
// The search is a slice per tick (step()), so the game keeps drawing.
#pragma once
#include <stdint.h>
#include "Game.h"

namespace ai {

enum Level : uint8_t { TOURIST, REGULAR, HIGH_ROLLER };

// Think about `side`'s move in the game as it stands.
void start(uint8_t side, uint8_t level);
// A slice of the search (budget: roughly words looked at). True when done.
bool step(uint16_t budget);
void stop();                            // enough: settle for the best found so far
uint16_t progress();                    // 0..256
// The decision: a play (true), or none worth making (false: swap or pass).
// hint: the best play found, however poor.
bool chosen(game::Play &pl, wd::Result &r, bool hint = false);
// With no play: the rack slots to throw back (0: pass).
uint8_t swapMask();
uint32_t tried();                       // placements scored (debug)

}  // namespace ai
