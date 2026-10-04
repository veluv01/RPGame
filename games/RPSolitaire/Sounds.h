// The game's sound effects, for the RPGame library's piezo sequencer
// (rpgame/Audio.h): short step lists, mostly from CHBlackjack (via CHChess).
// There is no music - the fanfares are effects too. Every effect GLIDEs (its
// sweeps change pitch at a wave cycle's end), as the game's own sequencer
// played them. A card landing on a foundation and a cascade bounce are
// audio::blip()s.
#pragma once
#include <rpgame/Audio.h>         // the RPGame library's sound engine

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Deal, Flip, Pick, Back, Suit, BigWin, Shuffle, Coin,
    Bust, Title, Whoosh,
    COUNT
};

// The effects, in Sfx order: audio::begin(SOUNDS, (uint8_t)Sfx::COUNT).
extern const audio::Effect SOUNDS[(int)Sfx::COUNT];
