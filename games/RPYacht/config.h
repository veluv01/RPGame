// CHYacht build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the board package 0.3.0+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer); release builds also set USB "Upload only" (no
// Serial). tools/device.py has the exact settings.
#pragma once

#define CHYD_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Device debug builds carry the protocol, so they may leave out things the
// tests never need (saving, the Options and Stats screens) to fit. Release
// builds and the simulator keep everything; -DCHYD_FULL forces a full
// device debug build.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHYD_FULL)
#define CHYD_LEAN        1
#else
#define CHYD_LEAN        0
#endif

#define CHYD_FPS         60
