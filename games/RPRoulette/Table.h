// The wall band: the back wall, the croupier (CHBlackjack's dealer), his
// speech bubble, the PURSE/BET plaque, the tote board and the rail.
#pragma once
#include <stdint.h>

namespace table {

enum Expr : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

void wall();
void dealer(uint8_t expr, uint8_t look, bool alt, int x = 2, int y = 0);
void rail();
void plaque(int32_t purse, int32_t bet, uint8_t purseFlash);
// The last numbers, newest on top (red right, black left, zero centred, as
// a casino's display board shows them).
void tote(const uint8_t *history, uint8_t n);
// Typewriter: each line is centred on its full width, then only the first
// `typed` characters are drawn.
void speechBubble(const char *text, int typed);

}  // namespace table
