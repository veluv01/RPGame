// Top-level screens: Title, Play (with its pause menu), Options, Stats and
// the ends of a game - the final scores, and going broke.
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // input + logic, before gfx_wait()
void render(uint32_t frame);        // after gfx_wait()

// Debug protocol hooks (CHYacht.ino).
bool debugCommand(char cmd, const char *args);

}  // namespace screens
