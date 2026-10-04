// The play screen: turns the table's events into motion and draws it all.
//
// Cards fly from the dealer's spot and flip (CHBlackjack's deal), chips fly
// to the bets, into the pot and to the winner, each seat's plate shows what
// it just did, a plate at the foot of the screen calls the action out
// (CHChess's word-drop plate), and the showdown lifts the winning five with
// a rainbow edge. The table waits while busy().
#pragma once
#include <stdint.h>
#include "Table.h"

namespace stage {

void reset();                               // a new table: nothing on it
void onEvents(Table &t);                    // drain the table's events
void update(const Table &t, uint32_t frame);
bool busy();                                // cards or chips still moving
// Draws the table (false: nothing changed, the last frame stands). ui: the
// screen's own state over it (pause menu...), to tell when to redraw.
bool render(const Table &t, uint32_t frame, uint32_t ui);
void invalidate();
const char *streetName(const Table &t);

}  // namespace stage
