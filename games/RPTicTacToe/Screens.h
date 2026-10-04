// Top-level screens: Title, the tables room, Play, Options, Stats, Win, Lose,
// with a pause menu and the rules card over play (CHBlackjack's skeleton).
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // input + logic, before gfx_wait()
void render(uint32_t frame);        // after gfx_wait()

// Debug hooks - see CHTicTacToe.ino.
void debugSeed(uint32_t seed);
void debugJump(char screen, uint8_t mode);
bool debugCell(uint8_t cell, uint8_t arg);
void debugPurse(int32_t purse);
void debugLevel(uint8_t level);
void debugClock(uint16_t ticks);
void debugDealer(uint8_t cell);

}  // namespace screens
