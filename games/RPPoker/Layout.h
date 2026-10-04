// Every coordinate of the 128x128 table in one place.
//
//    0..9    HUD: the game and stakes; the street, or who is thinking
//   11..20   the three CPU seats' plates: avatar chip, stack or last action
//   22..36   their cards (10x15 minis)
//   37..44   their bets, and the dealer button
//   46..73   the board (five 22x28 cards); Draw and Stud: the pot
//   74..81   your best hand (HINTS) and the pot
//   83..110  your cards, and your panel on the right
//  111       gold trim
//  112..127  the action bar, or the narration plate
//
// Seats go clockwise from you: 1 left, 2 across, 3 right.
#pragma once
#include <stdint.h>

namespace lay {

constexpr int HUD_H = 10;
constexpr int BLOCK_W = 42;
constexpr int PLATE_Y = 11, PLATE_H = 10;
constexpr int MINI_Y = 22;
constexpr int BET_Y = 38;                   // top of a seat's bet amount
constexpr int BOARD_Y = 46;
constexpr int POT_Y = 75;
constexpr int HAND_Y = 83;
constexpr int PANEL_X = 92, PANEL_Y = 84, PANEL_W = 35, PANEL_H = 26;
constexpr int TRIM_Y = 111, BAR_Y = 112, BAR_H = 16;
constexpr int DECK_X = 53, DECK_Y = 46;      // where the cards come from (the dealer)
constexpr int MUCK_X = 64, MUCK_Y = 40;      // where they go

inline int blockX(uint8_t seat) { return (seat - 1) * 43; }   // seats 1..3
inline int boardX(uint8_t i) { return 7 + 23 * i; }

}  // namespace lay
