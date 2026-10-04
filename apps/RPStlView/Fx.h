// Sizzle for CHStlView: the RPGame library's rpgame/Sizzle with this app's
// switches (the library's Sizzle.h lists them all), compiled in Fx.cpp.
//
// No particles: the viewer's frame time belongs to the card and the wires,
// and a spray of sparks over a model would only hide it. What is left is
// the part that suits a secret agent's watch: the pop-in banner (in the chrome's teal)
// for a mode change or a view reset, and the screen shake under a refusal.
#pragma once
#include <RPGame.h>

#define SIZZLE_CONFIGURED 1
#define SIZZLE_NO_PARTICLES 1
#define SIZZLE_KIND_SPARK 0
#define SIZZLE_KIND_STAR 0
#define SIZZLE_KIND_DUST 0
#define SIZZLE_BURST 0
#define SIZZLE_FLOATS 0
#define SIZZLE_HUES_EXPORT 0
#define SIZZLE_HOLD_BANNER 0
#define SIZZLE_STYLES SIZZLE_GREEN
#include <rpgame/Sizzle.h>
