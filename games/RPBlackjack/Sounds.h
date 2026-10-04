// The game's sounds, for the RPGame library's piezo sequencer
// (rpgame/Audio.h). PPOT's only sound was a blip on its splash screen (a
// Timer3 square wave); here every action has a voice. Effects are short
// step lists kept inside the piezo's 1-4 kHz sweet spot; music is Playtune
// scores (tools/make_music.py writes src/audio/Music.cpp), played as a
// lead line or an arpeggio as the options choose.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Deal, Flip, Chip, Cursor, Select, Deny, Win, Blackjack, Bust, Push, Lose,
    Peek, Shuffle, Coin, Split, Double, Insurance, Broke, Reveal, Whoosh, COUNT
};
enum class Song : uint8_t { Title, Victory, Broke };

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];

// Song s (its score from src/audio/Music.cpp) as the music: loop = repeat it at its end.
void playSong(Song s, bool loop);

// Sound has two switches: the options' mode (0 off, 1 arpeggio, 2 lead)
// and SELECT's mute during play. It sounds only while both allow it, and
// each keeps its setting while the other one is off.
namespace sound {
void setMode(uint8_t mode);
void mute(bool m);
bool muted();
}  // namespace sound
