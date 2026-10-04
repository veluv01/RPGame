// The play screen: the felt for each board shape, the marks, the gloves, and
// the show put on for what the match reports (strikes, tosses, the cat).
#pragma once
#include <stdint.h>
#include "Match.h"

namespace stage {

void reset(const Match &m);
void onEvents(const Match &m);      // after Match::update: its events become motion and sound
void update(const Match &m);        // once per logic tick
bool busy();                        // still showing something the match must wait for
void invalidate();                  // something drew over the screen: redraw it all
void paid(int32_t net);             // the result's money, floated by the purse
// The 3x3 and 5x5 tables are seen in iso; SELECT swaps to the flat map.
bool canIso(const Board &b);
bool isoOn(const Board &b);
void toggleView();
bool render(const Match &m, const Casino &c, uint32_t frame);   // false: nothing changed

// A mark on its own (the title's little board): sym 1 = X, 2 = O.
void mark(int cx, int cy, int r, uint8_t sym, uint8_t colour = 0);

}  // namespace stage
