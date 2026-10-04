// CHWords build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ and its default Peripherals setting ("Game",
// which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash). It is
// built with Optimize set to "Smallest + LTO" and, for release, USB set to
// "Upload only" (no Serial: ~0.6 KB); it fits without either.
#pragma once

#define CHWD_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Device debug builds carry the ~2 KB protocol and USB Serial, so they
// leave out what the measurements made with them never need: saving, the
// options and setup screens and the hint (games are started with the
// protocol's G command). The simulator (not flash-bound) and
// release builds keep everything.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHWD_FULL)
#define CHWD_LEAN        1
#else
#define CHWD_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHWD_FPS         60
