// The rules of two games played with the double-six set, for two players.
//
// DRAW: seven tiles each; a tile is played on either end of the line if it
// matches; with nothing to play you draw from the boneyard until you can
// (and pass once it is empty). The first to play their last tile scores the
// pips left in the other hand; if nobody can play, the lighter hand scores
// the difference.
//
// ALL FIVES: the same, and after each play the open ends are added up - a
// double at an end counts both its halves - and a total that divides by five
// scores that many points at once. A double played first is the spinner:
// once it has a tile on both sides, tiles may be played on its two free
// ends as well, four arms in all. What a round's end scores is rounded to
// the nearest five.
//
// No graphics here: the host tests play these rules against a second
// implementation (tools/tests).
#pragma once
#include <stdint.h>

namespace dom {

constexpr uint8_t TILES = 28, NONE = 0xFF;
constexpr uint32_t ALL = (1u << TILES) - 1;
enum Arm : uint8_t { E, W, N, S, ARMS };
enum Game : uint8_t { DRAW, FIVES };
enum Reason : uint8_t { DOMINO, BLOCKED };

// Tile a|b (a <= b) is number b * (b + 1) / 2 + a; PIPS[t] = b << 4 | a.
extern const uint8_t PIPS[TILES];
inline uint8_t lo(uint8_t t) { return PIPS[t] & 15; }
inline uint8_t hi(uint8_t t) { return PIPS[t] >> 4; }
inline bool isDouble(uint8_t t) { return lo(t) == hi(t); }
inline uint8_t pips(uint8_t t) { return (uint8_t)(lo(t) + hi(t)); }
inline uint8_t tileOf(uint8_t a, uint8_t b) { return (uint8_t)(a <= b ? b * (b + 1) / 2 + a : a * (a + 1) / 2 + b); }
uint8_t count(uint32_t tiles);
uint8_t handPips(uint32_t tiles);
uint8_t nth(uint32_t tiles, uint8_t k);         // the k-th tile of a set (0 = lowest), NONE past its end

struct Round {
    uint32_t hand[2], bone;         // sets of tiles
    uint8_t play[TILES];            // the line, in the order played: tile | arm << 5
    uint8_t n;
    uint8_t end[ARMS];              // the pips open at each arm's end
    uint8_t len[ARMS];              // tiles on each arm (the first tile is on none of them)
    uint8_t dbl;                    // bit a: arm a ends in a double
    uint8_t spinner;                // the first tile is one
    uint8_t turn, passes;
    uint8_t voids[2];               // bit v: the side has been seen without a v (it drew or passed)
    uint8_t game;
};

// A new round from 28 shuffled tiles: seven to each hand, the rest the boneyard.
void deal(Round &r, uint8_t game, const uint8_t *order, uint8_t first);
// The arms (a bit each) the tile may be played on.
uint8_t armsFor(const Round &r, uint8_t tile);
bool canPlay(const Round &r, uint8_t side);
// What the open ends add up to, as ALL FIVES counts them.
uint8_t endSum(const Round &r);
// The side's tile onto the arm: the points it scores at once.
uint8_t place(Round &r, uint8_t side, uint8_t tile, uint8_t arm);
void drew(Round &r, uint8_t side, uint8_t tile);    // from the boneyard into the hand
void passed(Round &r, uint8_t side);
// The arms worth offering for the tile: those that differ in what they score
// or leave open (the shortest of each kind). Returns how many.
uint8_t options(const Round &r, uint8_t tile, uint8_t *arms);
// Is the round over, and how.
bool over(const Round &r, uint8_t &reason);
// ... and who won it (2: nobody, a blocked game with equal hands) and what it scores.
uint8_t settle(const Round &r, uint8_t reason, uint8_t &winner);
// Who holds the heaviest double (failing any, the heaviest tile), and which: it is set first.
uint8_t opener(const Round &r, uint8_t &tile);
// The line's state (ends, arms) worked out again from the tiles played; false
// if they could not have been played so, or the sets are not the 28 tiles.
bool rebuild(Round &r);

// xorshift32: the deal, the boneyard, the CPU's coin.
struct Rng {
    uint32_t state;
    void seed(uint32_t s, uint32_t stream);
    uint32_t next();
    uint8_t below(uint8_t n);       // 0..n-1
};
void shuffle(uint8_t *order, Rng &rng);

}  // namespace dom
