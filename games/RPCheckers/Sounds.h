// The game's sounds, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short effects kept inside the piezo's 1-4 kHz sweet
// spot - each jump of a combo plays a couple of semitones higher - and the
// title's tune, a melody that loops under them (an effect sounds over it
// and the tune keeps time).
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Land, Hop, Capture, Coin, Chip, Crown,
    Whoosh, Sweep, Win, Lose, Draw, Turn,
    Tick, Tock,                      // soft (quieter than the rest): keep them last
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];

// The music: the title's tune (the MUSIC option), or none.
enum class Song : uint8_t { NONE, TITLE };
void playSong(Song s, bool loop = true);   // NONE stops the music
