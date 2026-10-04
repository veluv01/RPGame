// CHMahjong build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the board package 0.3.0+ with Optimize set to "Smallest + LTO"
// and the default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHMJ_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Device debug builds carry the ~3 KB protocol, and with it the game no
// longer fits: they leave out the EASY tile faces (TILES is CLASSIC only)
// and the particles (SIZZLE_NO_PARTICLES in Fx.h).
// The simulator (not flash-bound) and release builds keep everything.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHMJ_FULL)
#define CHMJ_LEAN        1
#else
#define CHMJ_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHMJ_FPS         60
