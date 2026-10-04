// Sparkle: a particle pool, pop-up banners and floating "+$15" texts. The
// easing, sine, randomness and screen shake are the RPGame library's
// (fx:: in rpgame/Fx.h). The rest is the library's rpgame/Sizzle, configured
// here (the switches that differ from its defaults) and compiled in Fx.cpp;
// the library's Sizzle.h lists every switch.
#pragma once
#include <RPGame.h>

#define SIZZLE_CONFIGURED 1
#define SIZZLE_KIND_COIN 1
#define SIZZLE_KIND_RAIN 1
#define SIZZLE_RAIN_KIND_NAME RAIN
#define SIZZLE_DUST 1
#define SIZZLE_HUES_EXPORT 0
#define SIZZLE_HOLD_BANNER 0
#define SIZZLE_BANNER_WRAP 0
#define SIZZLE_BANNER_FILL WHITE
#define SIZZLE_COLOUR_INLINE 1
#define SIZZLE_POOL 64
#define SIZZLE_BANNER_CHARS 16
#define SIZZLE_BANNER_ROWS_DOWN 38
#include <rpgame/Sizzle.h>

namespace fx {
void explode(int x, int y, uint8_t n);                                     // coins, every way at once
}  // namespace fx
