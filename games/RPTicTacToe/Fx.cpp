// Builds the RPGame library's rpgame/Sizzle (particles, banners, floating
// texts) under this file's size pragma, with the switches in Fx.h.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include "Fx.h"
#include <rpgame/Sizzle.inl>
