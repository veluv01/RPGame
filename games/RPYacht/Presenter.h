// The presentation layer of the play screen (CHBlackjack's, re-cut for a
// score card and a tray of dice).
//
// The rules (Yacht.cpp) change the card and the money at once; the
// presenter makes it visible at its own pace: the dice drop into the tray
// after the cam, a scored box flashes, bonuses rain coins, the purse rolls
// up, the next seat is announced. The play screen waits on busy() so nothing
// new starts before the show is over.
#pragma once
#include <stdint.h>

class Yacht;

namespace present {

enum Focus : uint8_t { F_NONE, F_DICE, F_ROLL, F_CARD };

void reset(const Yacht &g);         // everything in place, no show
void update(const Yacht &g);        // one 60 Hz tick
bool busy();                        // a score is being shown
void speedUp();                     // the player is in a hurry (A/B during the show)

void cursor(uint8_t focus, uint8_t index);
void deny();                        // the cursor shakes its head
void viewNext(const Yacht &g);      // look at the next seat's card
void setRollHeld(bool held);

void onRest(const Yacht &g);        // the dice stopped in the cam: name the roll
void onLanded(const Yacht &g, uint8_t thrown);   // back from the cam: dice into the tray
void onScore(const Yacht &g, uint8_t events);    // after Yacht::score()

// Drawing. render() redraws what changed (or the dice cam) and the bar.
void render(const Yacht &g, uint32_t frame);
void invalidate();
extern const char *const CAT_NAME[];
const char *seatName(const Yacht &g, uint8_t player);

}  // namespace present
