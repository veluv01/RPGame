// Poker hand evaluation, from 1 to 7 cards, with no lookup tables.
//
// A score is cat << 20 | five rank nibbles (most significant first), so a
// bigger score is a better hand, and equal scores split the pot. Over all
// five-card hands there are exactly 7,462 distinct scores (tools/tests).
//
// Straights and flushes only count with five cards or more; with fewer (a
// stud player's face-up cards) pairs, trips and quads still rank, which is
// how stud decides who acts first.
#pragma once
#include <stdint.h>

namespace hand {

enum Cat : uint8_t {
    HIGH_CARD, PAIR, TWO_PAIR, TRIPS, STRAIGHT, FLUSH, FULL_HOUSE, QUADS, STRAIGHT_FLUSH, CATS
};

uint32_t eval(const uint8_t *cards, uint8_t n);
// Omaha: the best hand using exactly two of the four hole cards and three of
// the board (nb >= 3). Stops early once it finds a hand better than `beat`
// (the Monte Carlo only needs to know who wins).
uint32_t omaha(const uint8_t *hole4, const uint8_t *board, uint8_t nb, uint32_t beat = 0xFFFFFFFFu);

// The five cards that make the best hand, as bit masks over the hole cards
// and the board (showdown highlights). exactTwo: Omaha's rule.
uint32_t bestFive(const uint8_t *hole, uint8_t nh, const uint8_t *board, uint8_t nb, bool exactTwo,
                  uint8_t &holeMask, uint8_t &boardMask);

inline uint8_t cat(uint32_t score) { return (uint8_t)(score >> 20); }
inline uint8_t topRank(uint32_t score) { return (uint8_t)((score >> 16) & 15); }

// "FULL HOUSE", "ROYAL FLUSH", "PAIR OF KINGS", "ACE HIGH"... into buf (>= 20).
char *describe(char *buf, uint32_t score);
const char *catName(uint8_t cat);

}  // namespace hand
