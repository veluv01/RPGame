// CHStlView build switches.
//
// Built like the games: board package 0.3.0+, Peripherals "Game", Optimize
// "Smallest + LTO" (the renderer has its own -O2, and its hot loops run
// from SRAM), USB "Upload only" for release. It fits with room to spare
// either way; the HUD shows each model's frame rate, and a debug build's
// H command reports the rest.
#pragma once

#define STLV_VERSION     "2.0"

// The RPGame library's switches: CHGAME_DEBUG (the serial debug protocol;
// always on in the simulator, on the board only in `rpgame build --debug`).
#include <rpgame/Config.h>

#define STLV_FPS         60
