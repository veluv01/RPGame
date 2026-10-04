// Sparkle: a particle pool, pop-up banners and floating "+$15" texts, on
// top of the RPGame library's fx:: (easing, integer sine, randomness and the
// screen shake). They are the library's rpgame/Sizzle, configured here (the
// switches that differ from its defaults) and compiled in Fx.cpp; the
// library's Sizzle.h lists every switch.
#pragma once
#include <RPGame.h>
#include "config.h"

#define SIZZLE_CONFIGURED 1
#define SIZZLE_NO_PARTICLES CHRL_LEAN    // a device debug build has no particles, to fit
#define SIZZLE_KIND_COIN 1
#define SIZZLE_KIND_RAIN 1
#define SIZZLE_BANNER_FILL WHITE
#define SIZZLE_COLOUR_INLINE 1
#define SIZZLE_STYLES (SIZZLE_RAINBOW | SIZZLE_GOLD | SIZZLE_RED | SIZZLE_CYAN | SIZZLE_WHITE | SIZZLE_BLACK | SIZZLE_GREEN)
#include <rpgame/Sizzle.h>
