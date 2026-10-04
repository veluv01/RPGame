// The game's sounds, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short effects kept inside the piezo's 1-4 kHz sweet
// spot - the show's buzzers, bell and audience, BANKRUPT's slide whistle -
// and the title tune, a Playtune score (tools/make_music.py writes
// Music.cpp). The soft effects (from CHChess), on a narrow pulse so they
// sit under everything else, are the wheel's pegs clicking past the pointer.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Whoosh, Coin, Buzzer, Bankrupt, LoseTurn, BuzzIn, Bell, Ooh, Solve,
    BigWin, Envelope, Flip, TimeUp, Lose,
    Tick, Tock,                      // soft (quieter than the rest): keep them last
    COUNT
};
enum class Song : uint8_t { Title };

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];

// Plays a song's score (Music.cpp; debug builds have none, so it stops).
void playSong(Song s, bool loop);
