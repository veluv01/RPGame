// Every coordinate of the 128x128 table in one place.
//
//   0..10   the seats: who is playing, their totals, whose turn
//  11..83   the score card of the seat in view: two columns of boxes
//  84..87   wooden rail
//  88..110  the tray: five dice, the held ones lifted
// 111       gold trim
// 112..127  the bar: the purse and ROLL (the bar's rows are CHBlackjack's)
#pragma once
#include <stdint.h>

namespace lay {

constexpr int HEAD_H = 11;
constexpr int CARD_Y = 11, CARD_H = 73;
constexpr int CELL_Y = 13, CELL_PITCH = 10, CELL_W = 61, CELL_H = 9;
constexpr int COL_X0 = 2, COL_X1 = 65;

constexpr int RAIL_Y = 84, RAIL_H = 4;
constexpr int TRAY_Y = 88, TRAY_H = 23;
constexpr int DIE = 17, DIE_X0 = 9, DIE_PITCH = 23, DIE_Y = 92, DIE_HELD_Y = 88;

constexpr int TRIM_Y = 111;
constexpr int BAR_Y = 112, BAR_H = 16;
constexpr int PURSE_X = 1, PURSE_W = 60;
constexpr int ROLL_X = 64, ROLL_W = 62;

constexpr int DEALER_X = 2, DEALER_Y = 0;                       // the lose screen's
constexpr int FACE_X = DEALER_X + 12, FACE_Y = DEALER_Y + 14;   // expression patch

}  // namespace lay
