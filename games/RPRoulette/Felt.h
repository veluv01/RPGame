// The betting layout printed on the felt (docs/design/layout.md section 1),
// drawn shifted ox pixels sideways while the camera whips to the wheel.
#pragma once
#include <stdint.h>

namespace felt {

void draw(bool us, int ox);
// Highlight a cell (inside x0..x1, y0..y1): its grid lines plus its inner
// top and bottom rows, in c (FX_B pulses gold to white for free).
void ring(int x0, int y0, int x1, int y1, uint8_t c, int ox);

}  // namespace felt
