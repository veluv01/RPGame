// Sparkle: a particle pool, pop-up banners and floating "+$15" texts, on
// top of the RPGame library's fx:: (easing, integer sine and randomness).
// The particle pool, the banner and the floats are the RPGame library's
// rpgame/Sizzle, configured here (the switches that differ from its defaults)
// and compiled in Fx.cpp; the library's Sizzle.h lists every switch.
#pragma once
#include <RPGame.h>
#include "config.h"

#define SIZZLE_CONFIGURED 1
#define SIZZLE_NO_PARTICLES CHWW_LEAN    // a device debug build has no particles, to fit
#define SIZZLE_KIND_SPARK 0
#define SIZZLE_KIND_STAR 0
#define SIZZLE_KIND_DUST 0
#define SIZZLE_KIND_COIN 1
#define SIZZLE_BURST 0
#define SIZZLE_HOLD_BANNER 0
#define SIZZLE_SHAKE 0
#define SIZZLE_STYLES (SIZZLE_RAINBOW | SIZZLE_GOLD | SIZZLE_RED | SIZZLE_CYAN | SIZZLE_WHITE | SIZZLE_GREEN)
#define SIZZLE_BANNER_FILL WHITE
#include <rpgame/Sizzle.h>
