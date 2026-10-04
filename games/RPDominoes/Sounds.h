// The game's sound effects, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short step lists kept inside the piezo's 1-4 kHz sweet
// spot - bone on wood, knuckles for a pass, a rising run for points, the
// boom of the last firecracker. There is no music: the fanfares are effects
// too. The crackers' climbing cracks, the deal and the counting scores are
// audio::blip()s made up in Stage.cpp.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Land, Lift, Coin, Score, Whoosh, Knock, Boom,
    Match, Win, Lose, Turn, Title,
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
