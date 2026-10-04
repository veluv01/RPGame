// The game's sound effects, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short step lists kept inside the piezo's 1-4 kHz sweet
// spot; the clock's Tick and Tock are soft (a narrow pulse, under
// everything else). There is no music, only the title's sting, an effect
// like the rest: there is no flash for a score.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Chip, Coin, Whoosh, Win, BigWin, Lose, Broke,
    Place, Poof, Tic, Tac, Meow, Title, Boom,
    Tick, Tock,                      // soft (quieter than the rest)
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
