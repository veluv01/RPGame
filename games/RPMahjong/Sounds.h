// The game's sounds, played by the RPGame library's engine (rpgame/Audio.h).
//
// Short effects kept inside the piezo's 1-4 kHz sweet spot: tiles clacking,
// a match chime that climbs with the streak, the shuffle's wash, the
// sparrow's chirp. There is no music - the fanfares (CHBlackjack's
// BLACKJACK one among them) are effects too. The tables are in Sounds.cpp.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Pick, Drop, Clack,
    Match1, Match2, Match3, Match4, Match5,      // a pair taken: higher with the streak (keep in order)
    Coin, Hint, Undo, Shuffle, Stuck, Jackpot, Win, Title, ZoomIn, ZoomOut, Chirp,
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
