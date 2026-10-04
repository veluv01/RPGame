// CHChess build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHCH_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Device debug builds carry the ~2 KB protocol, so they leave out things the
// tests never need (saving, the options screen and its credits). The
// simulator (not flash-bound) and release builds keep everything;
// -DCHCH_FULL forces a full device debug build.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHCH_FULL)
#define CHCH_LEAN        1
#else
#define CHCH_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHCH_FPS         60
