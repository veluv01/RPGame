// The play screen's presentation: tiles dealt to the racks, carried to the
// line and set down, the scores called, the glove, the camera following
// the play close up, the HUD - and, when a round is won, the line
// going off tile by tile like a string of firecrackers.
//
// Match reports events; the stage turns them into motion and says busy()
// until it has shown them (CHBlackjack's Presenter, CHChess's Stage).
#pragma once
#include <stdint.h>

namespace stage {

void begin();
void update();                       // drain match events, advance motion (once per tick)
bool busy();                         // something is still being shown: the game waits
bool ready();                        // nothing is, and nothing is queued: the player may act
bool overShown();                    // the end of the round has been shown
// Draws the play scene; false if nothing changed since the last frame (the
// framebuffer still holds it). ui: a signature of what the caller draws on top.
bool render(uint32_t frame, uint32_t ui);
void invalidate();                   // redraw next frame
void profile(uint32_t *us);          // debug builds: time per section (felt, line, HUD + far rack, your rack, the rest)
void snap();                         // show match::round as it stands, at once

// The player's side of it (the play screen drives these).
uint8_t cursor();                    // the tile of the hand under the glove (dom::NONE: none)
void setCursor(uint8_t tile);
void choose(uint8_t tile, const uint8_t *arms, uint8_t n);  // the tile fits several ends: which?
bool choosing();
void chooseStep(int d);              // the next (1) or the last (-1) of them
uint8_t chosen();                    // the arm the glove is on
void unchoose();
void hint(uint8_t tile, uint8_t arm);    // light the tile and where it goes
void deny();                         // A on a tile that fits nowhere: the buzz, and the glove flashes red
bool waiting();                      // held until a button: the hand-over between two players, the match's last word
void acknowledge();                  // ... pressed
void hurry();                        // a button while the line is going off: all at once
void setFast(bool on);               // QUICK pace: shorter pauses
void setOverview(bool on);           // the whole table, while it is on


}  // namespace stage
