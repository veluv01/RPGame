// The felt: bingo cards, the buy-in and the bar under them.
#pragma once
#include <stdint.h>

class Bingo;

namespace cards {

extern const uint8_t LETTER_COL[5];     // B I N G O
constexpr uint8_t DAUBERS = 5;
extern const uint8_t DAUB[DAUBERS];     // the dauber colours: red, blue, green, cyan, peach
uint8_t daub(const Bingo &g);           // the player's
// The glove with its cuff in the dauber's colour.
const uint8_t *cuff(uint8_t colour);

// How a card is lit this frame.
struct Look {
    uint32_t flash;         // cells just daubed (drawn bright)
    uint32_t win;           // the winning line (rainbow)
    bool     focused, deny; // deny: A found nothing to daub
};

// Card k with its left edge at x (it may hang off either side of the screen).
void draw(const Bingo &g, uint8_t k, int x, const Look &look);
// The centre of a cell of the card in play.
void cellXY(uint8_t cell, int &x, int &y);

void buyIn(const Bingo &g);
int  buyX(uint8_t i);                // the left edge of the buy-in's card i
// frozen: the caller is held (FREEZE).
void bar(const Bingo &g, bool frozen);

}  // namespace cards
