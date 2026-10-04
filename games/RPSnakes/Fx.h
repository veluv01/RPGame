// Sparkle: a particle pool, pop-up banners, floating "+$200" texts, on top
// of the RPGame library's fx:: (easing, integer sine, randomness and the
// screen shake).
// The particle pool, the banner and the floats are the RPGame library's
// rpgame/Sizzle, configured here (the switches that differ from its defaults)
// and compiled in Fx.cpp; the library's Sizzle.h lists every switch.
#pragma once
#include <RPGame.h>

#define SIZZLE_CONFIGURED 1
#define SIZZLE_KIND_COIN 1
#define SIZZLE_KIND_ORDER SPARK, CONFETTI, STAR, DUST, COIN
#define SIZZLE_COIN_FLOOR 114
#define SIZZLE_FOUNTAIN_GOLD_COINS 0
#define SIZZLE_FLOAT_BLINK_FIRST 1
#define SIZZLE_COIN_LATE 1
#include <rpgame/Sizzle.h>
