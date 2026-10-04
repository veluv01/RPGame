// The game's sound effects, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short step lists kept inside the piezo's 1-4 kHz sweet
// spot. The soft ones (from CHChess) play on a narrow pulse so they sit under
// everything else. There is no music: the flash goes to the game, and the
// hall is loud enough.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Chip, Coin, Whoosh, Win, BigWin, Lose, Broke,
    Ball, Power,
    Tick, Tock,                      // soft (quieter than the rest)
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
