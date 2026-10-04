// The play screen's presentation: dice thrown across the felt, checkers
// lifted, carried and set down, blots knocked to the bar, the gloves, the
// camera coming in close for the big moments, the HUD and the plate.
//
// Match reports events; the stage turns them into motion and says busy()
// until it has shown them (CHBlackjack's Presenter, CHChess's Stage).
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace stage {

void begin();
void update();                       // drain match events, advance motion (once per tick)
bool busy();                         // something is still being shown: the game waits
bool ready();                        // nothing is, and nothing is queued: the player may act
bool overShown();                    // the end of the game has been shown
// Draws the play scene; false if nothing changed since the last frame (the
// framebuffer still holds it). ui: a signature of what the caller draws on top.
bool render(uint32_t frame, uint32_t ui);
void invalidate();                   // redraw next frame
void profile(uint32_t *us);          // debug builds: us per section (board, checkers, dice + glove, HUD, fx)

// The player's side of it (the play screen drives these). A spot is where
// the glove can be: one of the side's points 1..24, its bar (bg::BAR), its
// tray (bg::OFF), its dice, or the cube.
constexpr uint8_t AT_DICE = 26, AT_CUBE = 27;
uint8_t cursor();
void setCursor(uint8_t spot);
void spotXY(uint8_t spot, int &x, int &y);      // where that is on the board (world)
void select(uint8_t from, const bg::Target *t, uint8_t n);  // pick the checker up: where it may go lights up
void deselect();
uint8_t selected();                  // the point of the picked-up checker, 0xFF: none
void setBlocked(bool b);             // the checker under the glove has no move (the plate says so)
void deny();                         // A on it anyway: the buzz, and the glove flashes red
bool waiting();                      // the last word of the game, held until a button
void acknowledge();                  // ... pressed
// The plate above the dice line: a hint (with the points its checkers go
// to, lit) or, to = nullptr, the coach's word; quiet() takes it down.
void advise(const char *text, uint8_t colour, const uint8_t *to, uint8_t n);
void quiet();
void setFast(bool on);               // QUICK pace: no close-ups, shorter pauses
extern const char *opponentName;     // the HUD's name for the CPU

void renderScene(uint32_t frame);    // the board and checkers only (the title screen's backdrop)

}  // namespace stage
