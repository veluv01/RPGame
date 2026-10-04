// The table: the glove, the cards in motion, and the win.
//
// The rules (Klondike.cpp) change at once; what the eye follows is
// flown here: every card that moves becomes a flier from where it was to
// where it now belongs, and its pile leaves it out until it lands. The
// glove carries a run of cards before any rule is asked - picking up is
// only a selection - so putting them back costs nothing.
//
// When the last card goes up the table stops being redrawn: the cascade
// stamps each bouncing card into the framebuffer and never wipes it, which
// is all the famous trail ever was.
#pragma once
#include <stdint.h>
#include "Klondike.h"

namespace stage {

enum State : uint8_t { DEALING, PLAY, AUTO, CASCADE, DONE };

void deal(const Klondike &k);               // a fresh deal: the cards fly out
void resume();                              // a table already in play
// pressed/rep: buttons just pressed, and auto-repeating. paused: the pause
// menu is up, the table only animates.
void update(Klondike &k, uint8_t pressed, uint8_t rep, bool paused);
void render(const Klondike &k, uint32_t frame);
void invalidate();                          // something drew over the table
State state();
uint8_t cursor();                           // the pile the glove is on (debug)
uint8_t holding();                          // cards in hand (debug)
void point(uint8_t pile, uint8_t depth);    // put the glove there (debug: scripts)
extern int32_t bank;                        // Vegas: the money before this game

}  // namespace stage
