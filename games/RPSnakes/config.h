// CHSnakes build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHSN_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// A build that leaves out saving and the options screen (about 2 KB). On
// for device debug builds, as in CHBoardwalk, whose framework this game
// started from (a full debug build of this game would fit: 39,940 B on
// 2026-10-02); release builds and the simulator have it all.
// -DCHSN_LEAN=0 in build.extra_flags gives a full device debug build.
#ifndef CHSN_LEAN
#if CHGAME_DEBUG && !defined(CHSIM)
#define CHSN_LEAN        1
#else
#define CHSN_LEAN        0
#endif
#endif

// Frame rate the game logic is paced for.
#define CHSN_FPS         60
