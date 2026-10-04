// The wheel as printed: 24 wedges a round, each three peg slots wide, so
// the wheel has 72 stops. Pure data.
//
// Every round shares one base layout; a round changes a few wedges (the
// top-dollar value, the mystery pair in round 2, the bankrupt-$10,000-
// bankrupt wedge in round 3). What a pick-up wedge turns into once its
// token is gone is the rules' business (Show::wedgeAt).
#pragma once
#include <stdint.h>

namespace wedge {

enum Kind : uint8_t {
    MONEY, TOP,             // value a consonant (TOP: the round's big one)
    BANKRUPT, LOSE, FREE,
    WILD, PRIZE,            // $500 a consonant, and a token if the letter is there
    MYSTERY,                // $1,000 a consonant, or flip it: $10,000 or bankrupt
    THIRDS,                 // bankrupt | $10,000 flat | bankrupt, by peg slot
    ENVELOPE,               // the bonus wheel (value unused)
};

constexpr uint8_t COUNT = 24, STOPS = 72;
constexpr uint16_t BIG = 10000, MYSTERY_VALUE = 1000, TOKEN_VALUE = 500, FREE_VALUE = 500;

struct Wedge {
    uint8_t  kind;
    uint16_t value;
};

// round 0..2 (the third is also Quick Play's only round).
Wedge at(uint8_t round, uint8_t index);

}  // namespace wedge
