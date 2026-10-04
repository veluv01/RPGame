// CHCrossword build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ and its default Peripherals setting ("Game",
// which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash). It is
// built with Optimize set to "Smallest + LTO" and, for release, USB set to
// "Upload only" (no Serial: ~0.6 KB); it fits without either.
#pragma once

#define CHCW_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// A device debug build carries the ~3 KB protocol and USB Serial, which the
// whole game no longer leaves room for: CHCW_LEAN leaves saving, the
// options screen and all but the first three built-in puzzles out of it
// (puzzles are started with the protocol's G command). Release builds and
// the simulator keep everything; -DCHCW_FULL forces a full device debug
// build, which does not fit.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHCW_FULL)
#define CHCW_LEAN        1
#else
#define CHCW_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHCW_FPS         60
