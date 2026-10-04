// The action bar along the bottom (PPOT's button rows, in colour).
#pragma once
#include <stdint.h>

class Round;

namespace bar {
bool draw(const Round &r, uint32_t frame);    // skips itself when nothing changed; true if drawn
void reset();
void invalidate();
}
