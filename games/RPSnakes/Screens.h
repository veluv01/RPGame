// The game's screens: title, setup, play (with its overlays), options.
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // once per logic tick
void render(uint32_t frame);        // once per drawn frame

}  // namespace screens
