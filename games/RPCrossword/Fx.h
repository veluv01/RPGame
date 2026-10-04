// Sparkle: a particle pool and pop-up banners, on top of the RPGame
// library's fx:: (easing, integer sine, randomness and the screen shake).
// The particle pool, the banner and the floats are the RPGame library's
// rpgame/Sizzle, configured here (the switches that differ from its defaults)
// and compiled in Fx.cpp; the library's Sizzle.h lists every switch.
#pragma once
#include <RPGame.h>

#define SIZZLE_CONFIGURED 1
#define SIZZLE_BANNER_DROP 1
#define SIZZLE_FLOATS 0
#define SIZZLE_KIND_DUST 0
#define SIZZLE_STYLES (SIZZLE_RAINBOW | SIZZLE_GOLD)
#include <rpgame/Sizzle.h>
