#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The library's rpgame/Sizzle, compiled here under this game's switches
// (Fx.h) and size pragma, and the daub's splat.
#include "Fx.h"
#include <rpgame/Sizzle.inl>

namespace fx {

// A daub: CHChess's landing puff along the felt, and gack - blobs of ink
// flung up and out, stretched along their flight, falling back under gravity.
void gack(int x, int y, uint8_t n, uint8_t colour) {
    burst(DUST, x, y + 2, 8, 26, colour);
    for (uint8_t i = 0; i < n; i++)
        spawn(GOO, x + rndRange(-2, 3), y, rndRange(-40, 41), rndRange(-70, -26), (uint8_t)rndRange(26, 44), colour);
}

}  // namespace fx
