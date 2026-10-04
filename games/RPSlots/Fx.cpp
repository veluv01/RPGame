#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The library's rpgame/Sizzle, compiled here under this game's switches
// (Fx.h) and size pragma, and the coin explosion.
#include "Fx.h"
#include <rpgame/Sizzle.inl>

namespace fx {

void explode(int x, int y, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        int a = (int)(rnd() & 255), sp = rndRange(30, 110);
        spawn(COIN, x, y, (isin(a + 64) * sp) >> 8, ((isin(a) * sp) >> 8) - 40, (uint8_t)rndRange(50, 90), GOLD);
    }
}

}  // namespace fx
