// The play screen's presentation: the pile, the glove, tiles in flight, the
// HUD and the plate. The rules live in MahjongBoard; this shows them, with the
// timing and the sparkle.
#pragma once
#include <stdint.h>
#include "config.h"

namespace stage {

void begin();
void setQuick(bool on);              // PACE: no deal animation, shorter flights
void setFaces(bool numbers);         // the EASY faces (numbers) or the classic ones

void deal(uint8_t layout, uint32_t seed);    // a new table: shuffled, then dealt in
void resume();                       // the board as it stands (a saved game)

void update(bool playing);           // once a tick; playing: the clock runs
void setZoom(bool close);            // the close-up: tiles twice the size round the glove
bool zoomed();
// Draws the table unless nothing has changed (then the last frame is shown
// again, and the palette still animates). ui: anything drawn over it.
bool render(uint32_t frame, uint32_t ui);
void invalidate();

bool busy();                         // dealing, tiles in flight, a banner: no input
uint8_t cursor();
uint8_t selected();
void hop(int ux, int uy);            // the D-pad: on to the next free tile that way
void press();                        // A: pick up, put down, or take the pair
void back();                         // B: put down, else undo the last pair
void hint();                         // SELECT: show a pair (for chips)
bool undo();
bool shuffle();                      // deal the tiles left again (for chips)
bool take(uint8_t a, uint8_t b);     // as picking a then b (scripts)
bool hinted(uint8_t &a, uint8_t &b); // the pair the hint is showing

bool clearedShown();                 // the table is cleared and the celebration is over
void shoo();                         // a button while the sparrow visits: off it flies
bool stuckShown();                   // no pair left, and the banner has said so

#if CHGAME_DEBUG
void profile(uint32_t *us);          // render cost by section (table, pile, hud, fx)
#endif

}  // namespace stage
