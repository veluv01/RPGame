// Every coordinate of the 128x128 play screen in one place.
//
// The wall and the rail are CHBlackjack's; the felt below them holds the
// player's cards as a carousel that only ever shows one and a half: the
// card in play and the first three columns of the next one:
//
//   0..41   back wall: the caller, the PURSE/POT plaque (his speech bubble
//           covers it while he talks), the board of the last balls
//  42..45   wooden rail with the chip rack
//  46..111  the felt: the cards (or the buy-in)
// 112..127  the bar: a pip per card, the power meter, the jackpot
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
constexpr int TRAY_X = 54, TRAY_W = 44;                        // the chip rack

constexpr int TABLE_Y = 46;

// A card: a 2 px frame, the B-I-N-G-O header, five rows of five cells.
constexpr int CELL_W = 14, CELL_H = 10, HEAD_H = 8;
constexpr int CARD_W = 5 * CELL_W + 4, CARD_H = HEAD_H + 5 * CELL_H + 4;   // 74 x 62
constexpr int CARD_X = 4, CARD_Y = 48;                         // the card in play
constexpr int CARD_PITCH = CARD_W + 6;                         // the next one shows its frame and three columns

constexpr int BUY_Y = TABLE_Y + 20;                           // the buy-in's row of cards

constexpr int BAR_Y = 112, BAR_H = 16;
constexpr int BANNER_Y = 78;
constexpr int STRIP_Y = BANNER_Y - 18, STRIP_H = 35;            // the black band a banner stands on

}  // namespace lay
