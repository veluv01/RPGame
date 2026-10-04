// The game's sixteen colours: the house palette (the RPGame library's
// rpgame/Palette.h) with two slots given to the tiles, SKIN to BONE and CYAN
// to SLATE, so the rainbow and confetti have no cyan. A tile set is a swap of
// BONE and SLATE (table::useSet).
#pragma once
#include <RPGame.h>

enum : uint8_t { BONE = SKIN, SLATE = CYAN };    // the tiles' face and pips

extern const uint16_t COLOURS[16];               // for pal::init(), RGB444
