// The game's own panel shape, on top of the RPGame library's drawing.
#pragma once
#include <RPGame.h>

// fillRound in edge with fillRound in fill inside it: panels, plates,
// bubbles. Not the library's panel() (a fill inside a roundRect edge) or
// panelLit().
void edgedRound(int x, int y, int w, int h, uint8_t r, uint8_t fill, uint8_t edge);
