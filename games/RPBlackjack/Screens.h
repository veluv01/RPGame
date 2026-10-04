// Top-level screens, after PPOT's GameStateType: SplashScreen, TitleScreen,
// PlayGame, GameWin, GameLose - plus Options, Stats and a pause menu.
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // input + logic, before gfx_wait()
void render(uint32_t frame);        // after gfx_wait()

// Debug hooks (seed, stacked deck) - see CHBlackjack.ino.
void debugSeed(uint32_t seed);
void debugStack(const uint8_t *cards, uint8_t n);
void debugJump(char screen);

}  // namespace screens
