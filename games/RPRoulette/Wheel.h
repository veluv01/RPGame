// The two wheels: pocket order and number colours. Pure (no graphics).
//
// A "number" is 0..36, with 37 meaning 00, everywhere in the rules, the
// tote board, the save and the debug protocol. Only the wheel renderer and
// the ball work in wheel indexes (clockwise from the zero), through
// numberAt() and indexOf().
#pragma once
#include <stdint.h>

namespace wheel {

constexpr uint8_t N00 = 37;                 // the number 00
enum Colour : uint8_t { GREEN, RED_NUM, BLACK_NUM };

inline uint8_t pockets(bool us) { return us ? 38 : 37; }
uint8_t numberAt(uint8_t index, bool us);   // wheel index -> number
uint8_t indexOf(uint8_t number, bool us);   // number -> wheel index
Colour colour(uint8_t number);
// "17", "0" or "00"; returns the end of the text.
char *name(char *p, uint8_t number);

}  // namespace wheel
