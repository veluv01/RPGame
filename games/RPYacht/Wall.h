// The pinstriped wall behind the seats, the wooden rail, and CHBlackjack's
// dealer, who plays the house's hand and sees a broke player out.
#pragma once
#include <stdint.h>

namespace wall {

enum Expr : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

void backdrop(int rows);
void dealer(uint8_t expr, uint8_t look, int x = 2, int y = 0);
void rail(int y);                   // four rows: gold, wood, wood, ink

}  // namespace wall
