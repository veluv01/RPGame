#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The library's rpgame/Sizzle (particles, banners, floats), compiled here
// under this file's size pragma and Fx.h's switches.
#include "Fx.h"
#include <rpgame/Sizzle.inl>
