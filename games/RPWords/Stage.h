// The play screen: the board, the rack, the scores, and the show a play
// puts on (tiles slamming down, the word lighting up, the score floating
// off). The screens (Screens.cpp) own the input; what the player is doing
// with the tiles lives here because it is what gets drawn.
//
// A camera looks at the board: close up (16 px squares, the tiles' letters
// in a serif face, the premium squares labelled) while you play, the whole
// board (8 px squares) while the CPU thinks, while B is held and when the
// game is over; it whips between the two. The rack slides away when the
// whole board needs the screen.
#pragma once
#include <stdint.h>
#include "Game.h"

namespace stage {

enum Mode : uint8_t {
    BOARD,          // moving over the board
    RACK,           // choosing the tile for the square under the cursor
    PICK,           // choosing the letter a blank stands for
    SWAP,           // marking tiles to throw back
};

// What the player has laid out this turn: the tiles, and the rack slot each
// came from.
extern game::Play tent;
extern uint8_t tentSlot[wd::RACK];
extern int16_t tentScore;           // what it would score; -1: not a legal play yet
extern uint8_t cursor;              // a square
extern uint8_t rackSel;             // a rack slot
extern uint8_t pickSel;             // a letter 0..25
extern uint8_t swapMarks;
extern uint8_t mode;
extern bool down;                   // the way the word runs
extern uint8_t viewSide;            // whose rack is shown
extern bool peek;                   // the whole board, asked for (B held)

void begin();
void newGame();                     // a game was started or loaded
void retally();                     // the laid-out tiles changed: tentScore again
bool slotFree(uint8_t slot);        // a tile in the rack, not laid out
void placed(uint8_t cell);          // a tile was just laid there: it slams down

// The show for game::last (a play, a swap, a pass), and for a play refused.
void show(bool byCpu);
void deny(const wd::Span *word);    // shake; the word's squares flash (or the laid-out tiles)
void note(const char *text, uint8_t colour, uint8_t frames = 110);
void thinking(const char *text);     // a plate with the search's progress under it; nullptr: off

void update();
bool busy();                        // a show is on: the game waits
void invalidate();
// Draws the play screen if anything changed since the last frame drawn
// (`ui`: the screens' own state, so their overlays count). False: nothing
// was drawn, and the old frame goes out again.
bool render(uint32_t frame, uint32_t ui);
void renderTitle(uint32_t frame);   // the board close up, the camera drifting over it

}  // namespace stage
