// The game's sound effects, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short step lists kept inside the piezo's 1-4 kHz sweet
// spot. There is no music - the fanfares are effects too (flash is spent on
// the chess). Tick and Tock, the CPU's clock, are played soft.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Land, Hop, Capture, Coin, Check, Castle, Promote,
    Whoosh, Flip, Mate, Win, Lose, Draw, Turn, Title,
    Tick, Tock,                      // soft: quieter than the rest
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
