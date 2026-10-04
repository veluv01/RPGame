// CHBoardwalk build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHBW_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// A build that leaves out saving and the options screen (about 2 KB). On
// for device debug builds, which carry the ~2 KB protocol and no longer fit
// beside everything; release builds and the simulator have it all.
#ifndef CHBW_LEAN
#if CHGAME_DEBUG && !defined(CHSIM)
#define CHBW_LEAN        1
#else
#define CHBW_LEAN        0
#endif
#endif

// Frame rate the game logic is paced for.
#define CHBW_FPS         60
