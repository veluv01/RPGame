// Sizzle for CHSDtoUSB: the RPGame library's rpgame/Sizzle with this
// sketch's switches (the library's Sizzle.h lists them all), compiled in
// Fx.cpp.
//
// No particles: every millisecond the screen takes is a millisecond the
// card is not serving the PC (the instrument panel had sparks; they are
// gone). What is left draws only for an instant: a banner over the graph
// for the big events (read-only on or off, a format, new partitions, a size
// milestone, a block given up on). No screen shake either: its row shifter
// runs from SRAM (~400 B), and RAM is what this sketch has least of.
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
#define SIZZLE_SHAKE 0
#define SIZZLE_STYLES (SIZZLE_GREEN | SIZZLE_RED)
#include <rpgame/Sizzle.h>
