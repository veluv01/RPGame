// Sparkle: a particle pool and pop-up banners, on top of the RPGame
// library's fx:: (easing, integer sine, randomness and the screen shake).
// The particle pool and the banner are the RPGame library's rpgame/Sizzle,
// configured here (the switches that differ from its defaults: no floats)
// and compiled in Fx.cpp; the library's Sizzle.h lists every switch.
#pragma once
#include <RPGame.h>

#define SIZZLE_CONFIGURED 1
#define SIZZLE_FLOATS 0
#include <rpgame/Sizzle.h>
