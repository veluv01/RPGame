// The play screen's presentation: the camera, the pointing fingers, chips
// that lift, hop and land, captures knocked spinning off to the trays,
// combos, crownings, tile highlights, the end of the game, the HUD and the
// top-down strategy view.
//
// Match reports events; the stage turns them into motion and says busy()
// until it has shown them (CHBlackjack's Presenter, by way of CHChess).
#pragma once
#include <stdint.h>

namespace stage {

void begin();
void update();                       // drain match events, advance motion (once per tick)
bool busy();
bool moving();                       // a hop is still being played (the next hop of a multiple jump waits for no more)
bool overShown();                    // the end of the game has been shown
// Draws the play scene; false if nothing changed since the last frame (the
// framebuffer still holds it). ui: a signature of what the caller draws on top.
bool render(uint32_t frame, uint32_t ui);
void invalidate();                   // redraw next frame
void profile(uint32_t *us);          // debug builds: us per section (table, board, overlays, pieces, HUD, fx)

// The player's side of it (the play screen drives these).
uint8_t cursor();
void setCursor(uint8_t sq);
bool flipped();                      // viewing from Black's side
void select(uint8_t sq, const uint8_t *to, const uint8_t *cap, uint8_t n);
void deselect();
uint8_t selected();                  // the player's picked-up piece, 0xFF: none
bool waiting();                      // the last banner of the game, waiting for a button
void acknowledge();                  // ... pressed
// Why the piece under the glove cannot be played (the plate says so).
enum Why : uint8_t { FREE, NO_MOVES, MUST_JUMP, KEEP_JUMPING };
void setBlocked(uint8_t why);
void deny(uint8_t why);              // A on it anyway: the buzz, the reason and the glove flash red
// Views: the iso board and the flat map. The iso camera whips in close on
// each move (not at QUICK pace) and pulls back out.
enum View : uint8_t { NORMAL, MAP, VIEWS };
uint8_t view();
void setView(uint8_t v);
void setZoom(uint8_t tileH);         // iso zoom now, 5..10
// Inspection (B held on the iso board): zoomed right in; dx, dy -1/0/1 in
// screen space push the view to the board's edge or corner that way.
void inspect(bool on, int dx, int dy);
void thinkPick();                    // mid-search: the CPU's glove to the piece it is weighing
void setFast(bool on);               // quicker CPU turns and moves
extern const char *opponentName;     // the HUD's name for the CPU

// The title's backdrop: the board close up, no glove, the chips dropping
// onto it; the camera goes where drift() says unless a move is being shown.
void demo(bool on);
void drift(int x, int y);
bool introDone();                    // the chips are all down
void renderScene(uint32_t frame);    // the table, board, chips and sparks only

}  // namespace stage
