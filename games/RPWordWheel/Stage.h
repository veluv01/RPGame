// The wall band: the back wall, the host (CHBlackjack's dealer), his speech
// bubble, and the used-letter rack with its status line.
#pragma once
#include <stdint.h>

namespace stage {

enum Expr : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

void wall();
void dealer(uint8_t expr, uint8_t look, bool alt, int x = 2, int y = 0);
// Typewriter: each line is centred on its full width, then only the first
// `typed` characters are drawn. Up to four lines of 18.
void speechBubble(const char *text, int typed);
// The 26 letters, three rows of nine: called ones dim, vowels in cyan. The
// last cell holds the wild card when someone has it. status goes top left,
// right top right (in gold; flash: in white).
void rack(uint32_t used, const char *status, const char *right, bool wild, bool flash);

}  // namespace stage
