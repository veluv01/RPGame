// Every coordinate of the 128x128 table in one place.
//
// The rows are CHBlackjack's, so the two tables sit in the same casino:
//
//   0..41   back wall: the dealer (here the stickman), plaque, roll history
//  42..45   wooden rail with the dealer's chip rack
//  46..110  the craps layout (Zones.cpp has every spot)
// 111       gold trim
// 112..127  the bar: chips and ROLL
#pragma once
#include <stdint.h>

namespace lay {

constexpr int WALL_H = 42;
constexpr int DEALER_X = 2, DEALER_Y = 0;          // even x: fast blits
constexpr int FACE_X = DEALER_X + 12, FACE_Y = DEALER_Y + 14;   // expression patch

constexpr int PLAQUE_X = 52, PLAQUE_Y = 3, PLAQUE_W = 48, PLAQUE_H = 34;
constexpr int BUBBLE_X = 50, BUBBLE_Y = 2, BUBBLE_W = 51, BUBBLE_H = 35;   // over the plaque
constexpr int BOARD_X = 103, BOARD_Y = 3, BOARD_W = 23, BOARD_H = 34;      // roll history

constexpr int RAIL_Y = 42, RAIL_H = 4;
constexpr int TRAY_X = 54, TRAY_W = 44;                        // dealer's chip rack

constexpr int FELT_Y = 46;
constexpr int TRIM_Y = 111;
constexpr int BAR_Y = 112, BAR_H = 16;

// Where chips come from and go to.
constexpr int TRAY_CX = TRAY_X + TRAY_W / 2, TRAY_CY = RAIL_Y + RAIL_H + 2;

}  // namespace lay
