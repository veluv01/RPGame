// Top-level screens: Title, Setup (who stands at the podiums), Play,
// Options, Stats, the end of an episode, and a pause menu over play
// (CHBlackjack's skeleton).
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // input + logic, before gfx_wait()
void render(uint32_t frame);        // after gfx_wait()

// Debug hooks - see CHWordWheel.ino.
void debugSeed(uint32_t seed);
void debugStop(uint8_t stop);
void debugJump(char screen);
void debugPuzzle(uint8_t section, uint16_t index);     // the next puzzle fetched
void debugCall(char letter);
void debugSolve(bool right);
void debugCash(uint8_t player, int32_t cash);
void debugStep(uint8_t step);                          // jump the episode to a step
void debugKinds(uint8_t k0, uint8_t k1, uint8_t k2);
void debugState(char *buf);                            // one line describing the game

}  // namespace screens
