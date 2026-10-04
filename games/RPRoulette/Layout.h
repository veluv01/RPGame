// Every coordinate of the 128x128 play screen in one place.
//
// The wall and the rail are CHBlackjack's; the table band below them holds
// either the betting layout or the wheel, and the camera whips between the
// two (docs/design/layout.md, wheel.md):
//
//   0..41   back wall: the croupier, the PURSE/BET plaque (his speech bubble
//           covers it while he talks), the tote board of the last numbers
//  42..45   wooden rail with the croupier's chip rack
//  46..127  the table band:
//           betting: number grid 48..78, dozens 79..87, even money 89..97,
//                    the plate 100..110, the action bar 112..127
//           wheel:   the tilted wheel, centre (64, 86)
#pragma once
#include <stdint.h>

namespace lay {

constexpr int WALL_H = 42;
constexpr int DEALER_X = 2, DEALER_Y = 0;          // even x: fast blits
constexpr int FACE_X = DEALER_X + 12, FACE_Y = DEALER_Y + 14;   // expression patch

constexpr int PLAQUE_X = 52, PLAQUE_Y = 3, PLAQUE_W = 48, PLAQUE_H = 34;
constexpr int BUBBLE_X = 50, BUBBLE_Y = 2, BUBBLE_W = 51, BUBBLE_H = 35;   // over the plaque
constexpr int TOTE_X = 103, TOTE_Y = 3, TOTE_W = 23, TOTE_H = 37;

constexpr int RAIL_Y = 42, RAIL_H = 4;
constexpr int TRAY_X = 54, TRAY_W = 44;                        // the croupier's chip rack

constexpr int TABLE_Y = 46;                                    // the band the camera pans

constexpr int GRID_Y = 48;                                     // number grid 48..78
constexpr int PLATE_Y = 100;
constexpr int BAR_Y = 112, BAR_H = 16;

constexpr int WHEEL_CX = 64, WHEEL_CY = 86;

}  // namespace lay
