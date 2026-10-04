// Cards, chips and badges.
//
// PPOT composed each card from an outline bitmap plus a 3x5 rank and a 5x6
// suit glyph. Here the card is taller (22x28 - same width, same 12 px
// overlap strip) and drawn procedurally in colour: white face, ink edge,
// PPOT's suit glyphs in the corner under a bold rank, a big centre pip or a
// court portrait, and a patterned back.
#pragma once
#include <stdint.h>

namespace art {

extern bool fourColour;

uint8_t suitColour(uint8_t card);
// w < 22 squashes the card around its centre (the flip animation).
// full = draw the centre art (false for cards mostly covered by the next).
void card(int x, int y, uint8_t card, bool faceUp, int w = 22, bool full = true);
void cardSideways(int x, int y, uint8_t card);      // doubled-down card, 28x22
void dimCard(int x, int y, int w, int h);           // inactive split hand
void chipStack(int cx, int baseY, int32_t amount, uint8_t maxChips = 10);
void chip(int cx, int y, uint8_t denom, bool top);  // denom index 0..4 ($1..$100)
int  chipDenom(int32_t amount);                     // largest chip that fits
void badge(int x, int y, const char *text, uint8_t bg, uint8_t fg);
int  badgeWidth(const char *text);

}  // namespace art
