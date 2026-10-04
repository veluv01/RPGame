// The wall band: the back wall, the caller (CHBlackjack's dealer), his
// speech bubble, the PURSE/POT plaque, the board of the last balls and the
// rail.
#pragma once
#include <stdint.h>

namespace table {

enum Expr : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

void wall();
void dealer(uint8_t expr, uint8_t look, bool alt, int x = 2, int y = 0);
void rail();
void plaque(int32_t purse, int32_t pot, uint8_t purseFlash);
// The last balls called, newest on top, each in its letter's colour.
// balls = the draw, n = how many have been called.
void tote(const uint8_t *balls, uint8_t n);
// Typewriter: each line is centred on its full width, then only the first
// `typed` characters are drawn. big = one line at double size (a call).
void speechBubble(const char *text, int typed, bool big = false);

}  // namespace table
