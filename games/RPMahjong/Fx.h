// Sparkle: a particle pool, pop-up banners and floating "+$15" texts, on
// top of the RPGame library's fx:: (easing, integer sine, randomness and the
// screen shake).
// The particle pool, the banner and the floats are the RPGame library's
// rpgame/Sizzle, configured here (the switches that differ from its defaults)
// and compiled in Fx.cpp; the library's Sizzle.h lists every switch.
#pragma once
#include <RPGame.h>
#include "config.h"

#define SIZZLE_CONFIGURED 1
#define SIZZLE_NO_PARTICLES CHMJ_LEAN    // a device debug build has no particles, to fit
#define SIZZLE_POOL 64
#define SIZZLE_KIND_COIN 1
#define SIZZLE_DUST 2
#define SIZZLE_COIN_FLOOR_RUNTIME 1
#include <rpgame/Sizzle.h>
