// The back wall above the table: the stickman, the plaque, the roll history
// and the rail with the dealer's chip rack.
#pragma once
#include <stdint.h>

class Craps;

namespace wall {

enum Expr : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

void backdrop();
void dealer(uint8_t expr, uint8_t look, int x = 2, int y = 0);
void stick(int8_t waggle);
// The plaque: the purse, then the spot under the cursor - its name, what it
// pays, what's on it (or OFF / a hint in red).
void plaque(int32_t purse, uint8_t purseFlash, const char *name, const char *pays, int32_t bet,
            bool off);
void board(const Craps &g, uint8_t skip);       // roll history, the newest `skip` rolls held back
void rail();

}  // namespace wall
