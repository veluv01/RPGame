// Top-level screens: Title, Play, Options, Stats, Credits, Win, Lose, and a
// pause menu over play (CHBlackjack's skeleton).
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // input + logic, before gfx_wait()
void render(uint32_t frame);        // after gfx_wait()

// Debug hooks - see CHRoulette.ino.
void debugSeed(uint32_t seed);
void debugForce(uint8_t number);
void debugJump(char screen);
void debugGlove(uint8_t spot);
bool debugPlace(uint8_t spot, uint8_t amount);
void debugPurse(int32_t purse);

}  // namespace screens
