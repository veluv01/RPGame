// SRAM functions use the RP SDK .time_critical linker sections.
// Original macro names are retained for game source compatibility.
#pragma once

#if defined(ARDUINO_ARCH_RP2040) && !defined(CHSIM)
#define CHGAME_RAMFUNC(name) __attribute__((section(".time_critical.rpgame." #name), noinline))
#define CHGAME_APP_RAMFUNC(name) __attribute__((section(".time_critical.rpgame_app." #name), noinline))
#else
#define CHGAME_RAMFUNC(name) __attribute__((noinline))     // the simulator, host tests and tools
#define CHGAME_APP_RAMFUNC(name) __attribute__((noinline))
#endif

#ifndef RAMFUNC
#define RAMFUNC(name) CHGAME_APP_RAMFUNC(name)
#endif
