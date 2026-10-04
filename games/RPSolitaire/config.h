// CHSolitaire build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHSO_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// A device debug build that leaves out saving, for when the ~2 KB protocol
// no longer fits beside it. Not needed so far: opt in with -DCHSO_LEAN=1.
#ifndef CHSO_LEAN
#define CHSO_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHSO_FPS         60
