// The cards.
//
// Seven columns across 128 pixels leave 18 a column, so the table's card is
// 17x23: CHBlackjack's bold rank and Press Play On Tape's suit glyph side by
// side along the top (all a covered card shows), and below them a 9x9 pip or
// a court card's bust. The backs are the deck you pick: two weaves drawn in
// code and ten pictures (tools/art/backs), some of which move.
#pragma once
#include <stdint.h>

namespace art {

extern bool fourColour;
extern uint8_t backStyle;
extern uint8_t clock;              // frame / 16: the backs that move
extern bool flat;                  // no drop shadow (the cascade's stamps)

constexpr int SW = 17, SH = 23;
constexpr int BACKS = 12;
extern const char *const BACK_NAME[BACKS];

uint8_t suitColour(uint8_t card);
// w < SW squashes the card around its centre (the flip). rows < SH draws
// only that much of its top: a card the next one covers (a column of
// thirteen whole cards would cost the frame rate). edge: the outline.
void small(int x, int y, uint8_t card, bool faceUp, int w = SW, int rows = SH, uint8_t edge = 0);
void back(int x, int y, int w, uint8_t style, uint8_t edge = 0);
// An empty pile's outline; suit < 4 marks a foundation with its pip.
void slot(int x, int y, uint8_t suit = 0xFF);

}  // namespace art
