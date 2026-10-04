// Events -> motion: the croupier's face and speech, the glove, chips
// flying to and from the layout, the rolling purse, the whip to the wheel
// and back, the ball, and the payout. CHBlackjack's Presenter pattern: the
// rules (Roulette) never draw or play a sound; they wait while busy().
#pragma once
#include <stdint.h>

class Roulette;

namespace present {

void reset(const Roulette &r);      // a fresh table: what is shown = the rules' state
void onEvents(Roulette &r);         // drain the rules' events
void update(const Roulette &r);     // once per logic tick
bool busy();                        // the rules must wait for the show
void dismissBubble();
void fastForward(bool on);          // the ball at double speed (A held during the spin)
// Draw what changed; true if anything was drawn (then overlay() must run).
bool render(const Roulette &r, uint32_t frame);
void overlay(const Roulette &r, uint32_t frame);
void invalidate();                  // the next render redraws everything

}  // namespace present
