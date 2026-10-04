// The play screen's presentation: the disc hovering over the board and
// falling behind it, the dealer watching, blinking and talking, the camera
// coming in on the winning four as it lights up, and the endings - his
// congratulations when you win, a kind word when he does.
//
// The game reports events; the stage turns them into motion and says busy()
// until it has shown them (CHBlackjack's Presenter, CHChess's Stage).
#pragma once
#include <stdint.h>
#include "config.h"
#include "Rules.h"

namespace stage {

void begin();
void reset();                        // a new game is about to start
void update();                       // drain the game's events, advance motion (once per tick)
bool busy();                         // something is still being shown: the CPU waits
bool ready();                        // nothing is: the player may act
bool overShown();                    // the end of the game has been shown: the result may go up
bool ending();                       // ... and it was a full-screen ending
// Draws the play scene; false if nothing changed since the last frame (the
// framebuffer still holds it). ui: a signature of what the caller draws on top.
bool render(uint32_t frame, uint32_t ui);
void invalidate();                   // redraw next frame

uint8_t cursor();                    // the column your disc hovers over
void setCursor(uint8_t col);
void deny();                         // A on a full column: the buzz, and the disc shakes its head
void hurry();                        // a button while the ending plays: get on with it
void setFast(bool on);               // QUICK pace: no close-up, shorter pauses

// The plaque on the wall (while the dealer is not talking over it).
struct Plaque { const char *title, *labelA, *labelB; uint16_t a, b; };
extern Plaque plaque;

// The title screen's backdrop: a board mid-game, through the camera.
void demo();
void renderScene(uint32_t frame);
// The setup screen's wall: the dealer saying `text`.
void renderWall(uint32_t frame, const char *text, uint8_t face);
#if CHGAME_DEBUG
void showEnding(uint8_t winner);     // straight to an ending (the debug protocol)
#endif

}  // namespace stage
