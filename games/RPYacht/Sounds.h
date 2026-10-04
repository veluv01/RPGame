// The game's sound effects, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): every action has a voice, short step lists kept inside
// the piezo's 1-4 kHz sweet spot. The dice rattle and the score count-up are
// blips made up on the spot (audio::blip). No music yet: it goes in only if
// flash is left once the game is complete.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Chip, ChipTake, Throw, Bounce, Wall, Clack, Point, Win, BigWin,
    SevenOut, Craps, Coin, Sweep, Hot, Whoosh, Lose, Broke, COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
