// CHTicTacToe build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the board package 0.3.0+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHTT_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Device debug builds carry the protocol (~1.7 KB) and so leave out what
// the tests never need: the end screens' PPOT lettering (plain lettering
// instead), saving and the particles (SIZZLE_NO_PARTICLES in Fx.h). -DCHTT_FULL keeps them (it does not fit). The
// simulator and release builds keep everything.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHTT_FULL)
#define CHTT_LEAN        1
#else
#define CHTT_LEAN        0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHTT_FPS         60
