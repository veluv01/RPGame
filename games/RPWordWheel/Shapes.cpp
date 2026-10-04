#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")
// The game's own panel shape (Shapes.h).
#include "Shapes.h"

// A filled shape in the edge colour with the fill inside it.
void edgedRound(int x, int y, int w, int h, uint8_t r, uint8_t fill, uint8_t edge) {
    fillRound(x, y, w, h, r, edge);
    fillRound(x + 1, y + 1, w - 2, h - 2, (uint8_t)(r > 1 ? r - 1 : 1), fill);
}
