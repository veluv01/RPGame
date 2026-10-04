// Cards and chips.
//
// The big card is CHBlackjack's (derived from Press Play On Tape's drawCard):
// 22x28, white face, ink edge, a bold rank over PPOT's suit glyph in the
// corner, a big centre pip or a court portrait, a patterned back. New here:
// the 10x15 mini card the CPU seats show (a 3x5 rank over the suit), chips
// up to $1000, the seats' chip avatars and the dealer button.
#pragma once
#include <stdint.h>

namespace art {

extern bool fourColour;

constexpr int CARD_W = 22, CARD_H = 28, MINI_W = 10, MINI_H = 15;

uint8_t suitColour(uint8_t card);
// w < CARD_W squashes the card around its centre (the flip animation).
// full = draw the centre art (false for cards mostly covered by the next).
// edge: the outline (INK; FX_A for a winning card).
void card(int x, int y, uint8_t card, bool faceUp, int w = CARD_W, bool full = true, uint8_t edge = 0);
void mini(int x, int y, uint8_t card, bool faceUp, int w = MINI_W, uint8_t edge = 0);
void dim(int x, int y, int w, int h);                 // a card that doesn't play

void chip(int cx, int y, uint8_t denom, bool top);    // denom index 0..6 ($1..$1000)
void chipStack(int cx, int baseY, int32_t amount, uint8_t maxChips = 8);
int  chipDenom(int32_t amount);                       // largest chip that fits
void avatar(int cx, int cy, uint8_t colour);          // a seat's chip, seen from above
void button(int cx, int cy);                          // the dealer button

// CPU seat colours (avatar index -> palette, and the name it goes by).
extern const uint8_t SEAT_COLOUR[6];
extern const char *const SEAT_NAME[6];

}  // namespace art
