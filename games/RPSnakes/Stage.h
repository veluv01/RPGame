// The play screen's presentation: the camera, the board with its ladders,
// snakes and tokens, the dice, the ARCADE pick bar, the HUD and the plate at
// the foot of the screen.
//
// The game reports events; the stage turns them into motion and says busy()
// until it has shown them (CHBlackjack's Presenter, CHChess's Stage).
#pragma once
#include <stdint.h>

namespace stage {

extern const uint8_t SEAT_COLOUR[4];    // each seat's colour (its token's)

void begin();
void reset();                        // show the game as it stands, at once (no events)
void update();                       // drain game events, advance motion (once per tick)
bool busy();
bool overShown();                    // the end of the game has been shown
// Draws the play scene; false if nothing changed since the last frame (the
// framebuffer still holds it). ui: a signature of what the caller draws on top.
bool render(uint32_t frame, uint32_t ui);
void invalidate();                   // redraw next frame
void setFast(bool on);               // QUICK pace
// The title's backdrop: a game playing itself close up, with no HUD, dice,
// banners or sound.
void setDemo(bool on);

// The whole board, while the player looks (SELECT).
void setOverview(bool on);
bool overview();
// ARCADE: the two dice are down and the player at the turn is to choose.
bool picking();
uint8_t pickSel();
void setPick(uint8_t which);

// Debug: the camera, at once.
void lookAt(uint8_t square, uint8_t zoom);

}  // namespace stage
