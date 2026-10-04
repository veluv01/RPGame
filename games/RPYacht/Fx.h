// Sparkle: a particle pool, pop-up banners and floating "+$15" texts, on
// top of the RPGame library's fx:: (easing, integer sine, randomness and the
// screen shake).
// The particle pool, the banner and the floats are the RPGame library's
// rpgame/Sizzle, configured here (the switches that differ from its defaults)
// and compiled in Fx.cpp; the library's Sizzle.h lists every switch.
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
#include <rpgame/Sizzle.h>
