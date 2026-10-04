// The words: tile names and the cards' texts (our own wording, built from
// each card's effect so the two can never disagree).
#pragma once
#include <stdint.h>

namespace text {

// A tile's name at p ("KENTUCKY AVENUE"); returns the new end. sep goes
// between its last two words: ' ', or '\n' for a deed's two-line heading.
char *tile(char *p, uint8_t t, char sep = ' ');
// A card's text, lines separated by '\n' (up to 4 lines of 18).
char *card(char *p, uint8_t deck, uint8_t idx);
extern const char *const DECK_NAME[2];      // "CHANCE", "COMMUNITY CHEST"

}  // namespace text
