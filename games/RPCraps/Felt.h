// The craps layout printed on the felt (Zones.cpp says where).
#pragma once
#include <stdint.h>

class Craps;

namespace felt {

void draw(const Craps &g);
// The cursor's frame round spot i (in FX_A: the palette makes it pulse).
void hover(uint8_t table, uint8_t i, uint8_t colour);

}  // namespace felt
