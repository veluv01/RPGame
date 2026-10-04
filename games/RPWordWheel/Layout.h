// Every coordinate of the 128x128 play screen in one place.
//
//   0..41    back wall: the host; beside him the used-letter rack with its
//            status line (his speech bubble covers it while he talks)
//  42..50    board view: the category strip    wheel view: rim, pegs, pointer
//  51..98    board view: the puzzle board      wheel view: the wedges,
//  99..118   board view: the three podiums                 down to row 118
// 119..127   the prompt bar
//
// The letter picker slides over rows 99..127 (podiums and prompt bar).
#pragma once
#include <stdint.h>

namespace lay {

constexpr int WALL_H = 42;
constexpr int DEALER_X = 2, DEALER_Y = 0;          // even x: fast blits
constexpr int FACE_X = DEALER_X + 12, FACE_Y = DEALER_Y + 14;   // expression patch

constexpr int RACK_X = 50, RACK_Y = 2, RACK_W = 76, RACK_H = 38;        // and the bubble over it

constexpr int STRIP_Y = 42, STRIP_H = 9;

constexpr int BOARD_Y = 51, BOARD_H = 48;
constexpr int PANEL_W = 8, PANEL_H = 10, PITCH_X = 9, PITCH_Y = 11;
constexpr int PANEL_X0 = 1, PANEL_Y0 = BOARD_Y + 2;

constexpr int POD_Y = 99, POD_H = 20, POD_PITCH = 42, POD_W = 41, POD_X0 = 1;
constexpr int PROMPT_Y = 119, PROMPT_H = 9;
constexpr int PICK_Y = 99;

constexpr int RIM_Y = 42, WHEEL_Y0 = 51, WHEEL_Y1 = 118;
constexpr int POINTER_X = 64;

}  // namespace lay
