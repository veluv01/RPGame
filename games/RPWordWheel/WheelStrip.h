// The wheel in close-up: the shot of the pointer, with a few wedges and
// their printed values sweeping past it (rows 42..118).
//
// The wedges narrow toward a hub far below the screen. Each row's wedge
// edges come from stepping fixed-point positions (no division per row), and
// the fills are gfx_hline spans. Labels are stacked along each wedge's
// centre line; at speed they are left out and the band is dithered, which
// reads as motion blur.
#pragma once
#include <stdint.h>

class Show;

namespace wheelstrip {

// posQ8: the rim position under the pointer (Spin.h). speed: rim pixels
// moved this frame.
void draw(const Show &s, int32_t posQ8, int speed, uint32_t frame);

}  // namespace wheelstrip
