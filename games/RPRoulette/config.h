// CHRoulette build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the board package 0.3.0+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHRL_VERSION     "0.1"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol:
// screenshots, input injection, lockstep, perf; always on in the simulator,
// on the board only in `rpgame build --debug`) and CHGAME_PROFILE.
#include <rpgame/Config.h>

// Debug builds carry the protocol, so they leave out things the tests never
// need: every CHGAME_DEBUG build, the simulator included, has no music scores
// (src/audio/Music.cpp, from tools/make_music.py), and device debug builds
// (CHRL_LEAN) also draw the win and broke screens' titles in title35
// lettering instead of the PPOT bitmaps, drop the credits page (when
// CHRL_CREDITS is on) and the particles (Fx.h), unless built with -DCHRL_FULL. Saving stays.
// Release builds keep everything.
#if CHGAME_DEBUG && !defined(CHSIM) && !defined(CHRL_FULL)
#define CHRL_LEAN        1
#else
#define CHRL_LEAN        0
#endif

// CHBlackjack's back room, the croupier telling the credits (Stats, A).
// Off: it costs ~1.2 KB, and the game needs the flash; the credits are on
// the Options screen instead.
#ifndef CHRL_CREDITS
#define CHRL_CREDITS     0
#endif

// Attract mode: after ten idle seconds on the title (its tune played out)
// the croupier plays a few spins on his own. Off: ~0.8 KB the game needs.
#ifndef CHRL_DEMO
#define CHRL_DEMO        0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHRL_FPS         60
