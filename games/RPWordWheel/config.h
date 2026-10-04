// CHWordWheel build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the board package 0.3.0+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHWW_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Debug builds carry the protocol, so they leave out things the tests never
// need: every CHGAME_DEBUG build, the simulator included, has no music score
// (src/audio/Music.cpp, from tools/make_music.py), and device debug builds
// (CHWW_LEAN) also drop the Setup, Options and Stats screens - the podiums
// are set with the protocol's W command - and the particles (Fx.h), unless
// built with -DCHWW_FULL, which does not fit. Saving and the SD bank stay. Release builds keep
// everything.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHWW_FULL)
#define CHWW_LEAN        1
#else
#define CHWW_LEAN        0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHWW_FPS         60
