// The cards that pop up over the board: a title deed, and a Chance or
// Community Chest card. Drawn in code (panels and the 3x5 font), turning
// face up as they arrive like CHBlackjack's cards: a card is `open` 0..256
// wide, and blank until it faces you.
#pragma once
#include <stdint.h>

namespace cards {

constexpr int DEED_W = 62, DEED_H = 86, CARD_W = 82, CARD_H = 46;

// Text of several lines ('\n' between), each centred on cx. (Writes on s.)
void lines(int cx, int y, char *s, uint8_t c);

void deed(int cx, int y, uint8_t tile, int open = 256);                 // centred on cx, top at y
void card(int cx, int y, uint8_t deck, uint8_t idx, int open = 256);

}  // namespace cards
