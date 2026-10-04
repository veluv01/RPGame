// The screens: title, lobby, the table (with its pause menu), options,
// statistics, and the two endings - broke the bank, or broke.
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update(bool firstTick);        // firstTick: the first logic tick of this frame (the CPU thinks then)
void render(uint32_t frame);

}  // namespace screens
