// Top-level screens: Title, Play, Options, Stats, Lose, and a pause menu
// over play (CHBlackjack's skeleton).
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // input + logic, before gfx_wait()
void render(uint32_t frame);        // after gfx_wait()

// Debug hooks - see CHBingo.ino.
void debugSeed(uint32_t seed);
void debugJump(char screen);
bool debugGame(char cmd, uint32_t value);
void debugState(char *buf);         // "STATE ..." for the save test (64 bytes)

}  // namespace screens
