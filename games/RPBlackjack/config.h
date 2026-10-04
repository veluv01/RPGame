// CHBlackjack build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the board package 0.3.0+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHBJ_VERSION     "1.0"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Debug builds carry the ~2 KB protocol, so they leave out things the
// tests never need: every CHGAME_DEBUG build, the simulator included, has no
// music scores (src/audio/Music.cpp, from tools/make_music.py), and device
// debug builds also drop the credits page unless built with -DCHBJ_FULL.
// Release builds keep everything.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHBJ_FULL)
#define CHBJ_LEAN        1
#else
#define CHBJ_LEAN        0
#endif

// Frame rate the game logic is paced for (PPOT ran at 60).
#define CHBJ_FPS         60
