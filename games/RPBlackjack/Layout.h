// Every coordinate of the 128x128 table in one place.
//
// PPOT's 128x64 screen stacked dealer, player and buttons into 64 rows;
// here the dealer gets a proper back wall and the cards get room to breathe:
//
//   0..41   back wall: the dealer (PPOT's bust, recoloured), HUD plaque, shoe
//  42..45   wooden rail
//  48..75   dealer's cards
//  76..82   felt print "BLACKJACK PAYS 3 TO 2"
//  83..110  player's cards, bet circle to the right
// 111       gold trim
// 112..127  action bar
#pragma once
#include <stdint.h>

namespace lay {

constexpr int CARD_W = 22, CARD_H = 28, PITCH = 12;

constexpr int WALL_H = 42;
constexpr int DEALER_X = 2, DEALER_Y = 0;          // even x: fast blits
constexpr int FACE_X = DEALER_X + 12, FACE_Y = DEALER_Y + 14;   // expression patch

constexpr int PLAQUE_X = 52, PLAQUE_Y = 3, PLAQUE_W = 48, PLAQUE_H = 34;
constexpr int SHOE_X = 104, SHOE_Y = 20, SHOE_W = 22, SHOE_H = 26;
constexpr int SHOE_MOUTH_X = 106, SHOE_MOUTH_Y = 36;          // where cards leave

constexpr int RAIL_Y = 42, RAIL_H = 4;
constexpr int TRAY_X = 54, TRAY_W = 44;                        // dealer's chip rack

constexpr int DEALER_CARDS_Y = 48;
constexpr int DEALER_CX = 60;                                  // dealer hand centre
constexpr int PRINT_Y = 77;

constexpr int PLAYER_CARDS_Y = 83;
constexpr int PLAYER_CX = 48;                                  // single hand centre
constexpr int PLAYER_MAX_X = 94;                               // cards stay left of the bet circle
constexpr int SPLIT_CX0 = 25, SPLIT_CX1 = 72;

constexpr int BET_CX = 112, BET_CY = 100, BET_RX = 14, BET_RY = 8;
constexpr int INS_CX = 112, INS_CY = 62;                       // insurance bet

constexpr int TRIM_Y = 111;
constexpr int BAR_Y = 112, BAR_H = 16;

constexpr int BUBBLE_X = 50, BUBBLE_Y = 2, BUBBLE_W = 51, BUBBLE_H = 35;   // over the plaque

}  // namespace lay
