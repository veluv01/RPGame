// Top-level screens: Title, Play (with its pause menu), Options, Stats and
// the two ends of a game - breaking the bank, or going broke.
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // input + logic, before gfx_wait()
void render(uint32_t frame);        // after gfx_wait()

// Debug protocol hooks (CHSlots.ino).
bool debugCommand(char cmd, const char *args);

}  // namespace screens
