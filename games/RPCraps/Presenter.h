// The presentation layer of the play screen (CHBlackjack's, re-cut for dice).
//
// The rules (Craps.cpp) change the money at once; the presenter
// makes it visible at its own pace. Outside a roll it keeps the chips on
// the felt in step with the bets - a chip added flies down from the cursor,
// one taken flies back to the rack. After a throw it waits for the dice cam,
// then the stickman calls the roll and the dealer works the layout: losers
// swept into the rack, winners paid, come bets moved to their numbers, the
// puck turned and moved. The play screen waits on busy() so nothing new
// starts before the show is over.
#pragma once
#include <stdint.h>

class Craps;

namespace present {

void reset(const Craps &g);         // chips straight onto their spots, no show
void update(const Craps &g);        // one 60 Hz tick
bool busy();                        // a roll is being shown
void speedUp();                     // the player is in a hurry (A/B during the show)

// The throw: the rules have rolled and settled. The presenter takes over
// the dice cam's ending and the show after it.
void onThrow(const Craps &g);
// Betting feedback.
void cursor(uint8_t zone);
void denied(const Craps &g, uint8_t reason);
void say(const char *text, uint8_t face, uint8_t hold = 100);
void dismissBubble();
void tap();                         // the cursor chip drops (a chip went down)
void setDenom(uint8_t d);
void setRollHeld(bool held);
void setHoldBar(uint8_t w);         // hold-B progress, 0..128

// Drawing. render() redraws what changed (or the dice cam) and the bar.
void render(const Craps &g, uint32_t frame);
void invalidate();
int32_t shownPurse();

}  // namespace present
