// Every coordinate of the 128x128 table in one place.
//
//    1..23    the stock, the waste (fanned to the right when dealing three)
//             and, over columns 3..6, the four foundations
//   26..119   the seven columns, each squeezing its overlap to fit
//  120..127   the status line (a very long column runs over it)
#pragma once
#include "CardArt.h"

namespace lay {

constexpr int COL0 = 1, COL_DX = 18;        // 17 px cards, a pixel apart
constexpr int TOP_Y = 1, TAB_Y = 26;
constexpr int FAN_DX = 7;
constexpr int STATUS_Y = 120;
constexpr int UP_PITCH = 9, DOWN_PITCH = 3;  // a covered card shows this much
inline int colX(int i) { return COL0 + COL_DX * i; }

}  // namespace lay
