// Where the D-pad takes the cursor: it hops between the free tiles.
//
// UP and DOWN go to the nearest tile that way (CHChess's rule: straight
// ahead first, wrapping to the farthest the other way). LEFT and RIGHT step
// through the tiles as you would read them - along the row, then on to the
// next - because on a pile "nearest that way" alone can leave a tile no
// press reaches; this way LEFT or RIGHT visits every one (tools/tests proves
// it for every layout).
#pragma once
#include <stdint.h>

namespace nav {

// The tile in list (n of them) to go to from tile `from` (on the table or
// not), for a press of (ux, uy): one of (-1,0) (1,0) (0,-1) (0,1).
// board::NONE if the list has no other tile.
uint8_t step(const uint8_t *list, uint8_t n, uint8_t from, int ux, int uy);
// The tile in list nearest tile `from` (itself, if it is there).
uint8_t nearest(const uint8_t *list, uint8_t n, uint8_t from);

}  // namespace nav
