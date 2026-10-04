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
#define SIZZLE_HUES_NAME HUES
#define SIZZLE_DUST 2
#define SIZZLE_BANNER_CHARS 16
#define SIZZLE_FLOAT_CHARS 10
#define SIZZLE_FLOAT_CLAMP 1
#include <rpgame/Sizzle.h>
