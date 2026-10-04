// The whole board from above, with who owns what and where everyone stands;
// the players' cash and net worth in the middle. SELECT shows it.
#pragma once
#include <stdint.h>

namespace mapview {

void draw(uint32_t frame);          // rows 10..127 (the HUD stays above)

}  // namespace mapview
