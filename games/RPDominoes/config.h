// CHDominoes build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the board package 0.3.0+ and its default Peripherals setting ("Game",
// which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash). It is
// built with Optimize set to "Smallest + LTO" and, for release, USB set to
// "Upload only" (no Serial: ~0.6 KB); it fits without either.
#pragma once

#define CHDM_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// A build without saving, the options and setup screens and the hint (the
// title's ONE/TWO PLAYERS start a match on the defaults; tests start theirs
// with the protocol's G command): for a device debug build if the game
// should ever outgrow it. Off: everything fits, debug builds included.
#ifndef CHDM_LEAN
#define CHDM_LEAN        0
#endif

// Frame rate the game logic is paced for.
#define CHDM_FPS         60
