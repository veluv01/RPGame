// The dice cam: a full-screen view down a wooden dice tray to its padded
// back wall, where the throw plays out in 3D (CHCraps's cam, five dice).
//
//   begin()    fade from the table into the cam, the dice in the hand
//   (shaking)  the dice rattle in the foreground, the power bar swings
//   release()  the rules have rolled: the dice fly, bounce off the back
//              wall, tumble and settle on the rolled numbers
//   (result)   the camera cranes over them
//   leave()    fade back to the table
//
// Dice the player kept sit the throw out: they wait on the plate, top left.
#pragma once
#include <stdint.h>

namespace cam {

enum Phase : uint8_t { OFF, WHIP_IN, SHAKE, TUMBLE, RESULT, WHIP_OUT, ENTER };

// kept: bit i = die i stays back (dice: all five values, for the plate);
// rollNo 1..3; player picks the dice's colour; human: show the power bar.
void begin(uint8_t kept, const uint8_t *dice, uint8_t rollNo, uint8_t player, bool human);
void release(const uint8_t *v);         // throw now (or as soon as the whip-in ends): five values
void cancel();                          // dice back down, no throw
void leave();                           // RESULT -> back to the table
void skip();                            // jump the tumble to its end
void update();                          // one 60 Hz tick
Phase phase();
bool active();                          // the cam owns the screen
uint16_t restT();                       // ticks since the dice came to rest
// Draw the cam (true), or false if the table should draw itself.
bool render(uint32_t frame);

}  // namespace cam
