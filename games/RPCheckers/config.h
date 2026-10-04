// CHCheckers build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHCK_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Device debug builds carry the ~2 KB protocol, so they leave out things the
// tests never need (saving, the Options and Rules screens: Setup's RULES row
// steps through the rule sets instead). The simulator (not flash-bound) and
// release builds keep everything; -DCHCK_FULL forces a full device debug
// build.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHCK_FULL)
#define CHCK_LEAN        1
#else
#define CHCK_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHCK_FPS         60
