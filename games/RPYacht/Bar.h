// The bar along the bottom: the purse (or whose turn it is, in a party
// game) and the ROLL button with the rolls left.
#pragma once
#include <stdint.h>

namespace bar {

struct View {
    int32_t purse;              // < 0: none (a party game): `who` instead
    uint16_t ante;              // the chips riding on this game
    const char *who;
    const char *label;          // on the button
    uint8_t flash;              // the purse flashes as winnings land
    uint8_t rollsLeft;
    bool rollOn, sel, held;
};

// Skips itself when nothing changed; true if it drew.
bool draw(const View &v);
void invalidate();

}  // namespace bar
