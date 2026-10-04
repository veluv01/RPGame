// Every coordinate of the two machines in one place.
//
// Both show three rows of 24 px cells, so one reel drawer serves both, and
// both end in the series' 16 px bar:
//
//   LUCKY 7                          DRAGON FORTUNE
//    0..19  marquee and its bulbs     0..25  the four jackpot meters
//   20..97  chrome, three reels,     26..101 gold frame, five reels
//           the arm on the right
//   98..111 the win plaque          102..111 the win line
//  112..127 the bar: bet, purse     112..127 the bar: bet, SPIN, purse
#pragma once
#include <stdint.h>

namespace lay {

constexpr int CELL = 24, WIN_H = 3 * CELL;

// LUCKY 7
constexpr int C_TOP_H = 20;
constexpr int C_WIN_X = 8, C_WIN_Y = 23, C_REEL_W = 28, C_PITCH = 30;       // reels at 8, 38, 68
constexpr int C_BODY_W = 104;                                               // chrome to here, the arm beyond
constexpr int ARM_X = 116, ARM_TOP = 30, ARM_BOTTOM = 86, ARM_PIVOT = 62;   // the knob's travel
constexpr int C_LOW_Y = 98;

// SWEET: LUCKY 7's rows, the reels moved right to make room for the rush ladder
constexpr int S_WIN_X = 26;
constexpr int RUSH_X = 2, RUSH_W = 19;

// DRAGON FORTUNE
constexpr int F_TOP_H = 26;
constexpr int F_WIN_X = 4, F_WIN_Y = 28;                                    // reels 24 px apart
constexpr int F_LOW_Y = 102;

constexpr int BAR_Y = 112, BAR_H = 16;

}  // namespace lay
