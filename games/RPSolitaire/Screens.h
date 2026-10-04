// The screens: title, the table (with its pause menu and the win), the deck
// of card backs, options and statistics.
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();
void render(uint32_t frame);

}  // namespace screens
