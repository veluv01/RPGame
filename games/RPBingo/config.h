// CHBingo build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the board package 0.3.0+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHBN_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Device debug builds carry the protocol, so they leave out the broke
// screen's lettering unless built with -DCHBN_FULL. Saving stays. Release
// builds and the simulator keep everything.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHBN_FULL)
#define CHBN_LEAN        1
#else
#define CHBN_LEAN        0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHBN_FPS         60
