// Events -> motion: the caller's face and speech, the cards sliding past,
// the daubs, the rolling purse and the end of a round. CHBlackjack's
// Presenter pattern: the rules (Bingo) never draw or play a sound; between
// rounds they wait while busy().
#pragma once
#include <stdint.h>

class Bingo;

namespace present {

void reset(const Bingo &g);         // a fresh table: what is shown = the rules' state
void onEvents(Bingo &g);            // drain the rules' events
void update(const Bingo &g);        // once per logic tick
bool busy();                        // the rules must wait for the show
// The caller speaks: up to 4 lines of 12 characters, '\n' between; hold =
// ticks the bubble stays once typed.
void say(const char *text, uint8_t face, uint8_t hold);
void dismissBubble();
// Draw what changed; true if anything was drawn (then overlay() must run).
bool render(const Bingo &g, uint32_t frame);
void overlay(const Bingo &g, uint32_t frame);
void invalidate();                  // the next render redraws everything

}  // namespace present
