// The play screen's presentation: the camera, the board with its houses and
// tokens, the dice, cards, the auction, coins in flight, the HUD and the
// plate at the foot of the screen.
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
void setZoom(uint8_t tileH);         // iso zoom now, 5..10
void lookAt(uint8_t tile);           // the camera, at once
void setFast(bool on);               // QUICK pace

bool waiting();                      // a card or a banner stays up until a button
void acknowledge();                  // ... pressed
uint8_t picked();                    // the CPU's choice on show: game::Act + 1 (0: none)
// The caller has an action bar up (no plate at the foot of the screen).
void setBar(bool on);
// The manage view: the glove on this deed, its card up. -1: back to play.
void manage(int tile);

// The board, its buildings and tokens only (the title's backdrop); and the
// HUD alone (over the map).
void renderScene(uint32_t frame);
void hud(uint32_t frame);

}  // namespace stage
