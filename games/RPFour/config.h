// CHFour build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ and its default Peripherals setting ("Game",
// which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash). It is
// built with Optimize set to "Smallest + LTO" and, for release, USB set to
// "Upload only" (no Serial: ~0.6 KB); it fits without either.
#pragma once

#define CHF4_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// A build without saving and the options and setup screens (the sister
// games' device debug builds need it to fit the ~2 KB protocol and USB
// Serial; this one has the room, so debug builds are the whole game).
#ifndef CHF4_LEAN
#define CHF4_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHF4_FPS         60
