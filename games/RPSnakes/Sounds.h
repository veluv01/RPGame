// The game's sound effects, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short step lists kept inside the piezo's 1-4 kHz sweet
// spot, most of them CHBlackjack's. There is no music - the fanfares are
// effects too. The hops, rungs and the slide down a snake are audio::blip()s
// made up on the spot (Stage.cpp).
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Dice, Land, Coin, Ladder, Climb, Hiss, Chomp, Spit, Bump, Doubles,
    Whoosh, Win, Lose, Turn, Title,
    Tick, Tock,                      // soft (quieter than the rest): keep them last
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
