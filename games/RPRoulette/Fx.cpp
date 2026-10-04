#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The library's rpgame/Sizzle, compiled here under this game's switches
// (Fx.h) and size pragma.
#include "Fx.h"
#include <rpgame/Sizzle.inl>
