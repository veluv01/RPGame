// RPGame: the one include for a RPGame sketch.
//
//     #include <RPGame.h>
//
// brings, on top of RPGfx (the framebuffer, the panel and its DMA):
//
//   rpgame/Input.h    RPGame `rpgame`: buttons, frame pacing, START held
//                     3 s goes back to the SD game menu
//   rpgame/Palette.h  the house colours (INK, WHITE, FELT ... FX_A, FX_B) and
//                     pal:: (themes, fades, flashes, colour cycling)
//   rpgame/Draw.h     rounded panels, span sprites, dithering, the 3x5 font
//   rpgame/Mask.h     outlined, shadowed and gradient lettering and logos
//   rpgame/Fx.h       fx:: easing, integer sine, randomness, screen shake
//   rpgame/Audio.h    audio:: effects, music and the status LED (the piezo)
//   rpgame/Save.h     save:: a record in flash that survives power cycles
//   rpgame/Fmt.h      number formatting without printf
//   rpgame/RamFunc.h  RAMFUNC: code that runs from SRAM
//   rpgame/Debug.h    dbg:: the serial debug protocol the simulator and
//                     the tools drive a game through (CHGAME_DEBUG builds)
//   rpgame/Config.h   the library's build switches (CHGAME_DEBUG ...)
//
// The casino games in this library's examples are built from these.
#pragma once
#include <RPGfx.h>
#include "rpgame/Config.h"
#include "rpgame/Input.h"
#include "rpgame/RamFunc.h"
#include "rpgame/Palette.h"
#include "rpgame/Draw.h"
#include "rpgame/Mask.h"
#include "rpgame/Fx.h"
#include "rpgame/Audio.h"
#include "rpgame/Fmt.h"
#include "rpgame/Save.h"
#include "rpgame/Debug.h"

extern "C" void rpgame_enter_bootloader(void);
