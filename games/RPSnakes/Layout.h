// The board: a hundred squares, numbered from the bottom left and winding
// back and forth up to 100 at the top left, with eight ladders and eight
// snakes. Data and arithmetic only (the host tests check it is a fair board).
#pragma once
#include <stdint.h>

namespace layout {

constexpr uint8_t LAST = 100, LINKS = 16, LADDERS = 8;   // LINK: the ladders, then the snakes

// A ladder (from its foot up) or a snake (from its head down).
struct Link { uint8_t from, to; };
extern const Link LINK[LINKS];

// The ladder or snake that starts on square n, else -1.
int linkAt(uint8_t n);

// Rows count up from the bottom, columns from the left, both 0..9.
inline uint8_t row(uint8_t n) { return (uint8_t)((n - 1) / 10); }
inline uint8_t col(uint8_t n) {
    uint8_t c = (uint8_t)((n - 1) % 10);
    return row(n) & 1 ? (uint8_t)(9 - c) : c;
}
// The square straight below (where a bumped token drops); from the bottom
// row, square 1.
inline uint8_t below(uint8_t n) { return n <= 10 ? 1 : (uint8_t)(20 * row(n) + 1 - n); }

// Where a token on `from` ends up moving `steps`: past 100 it bounces back,
// and a ladder or snake there is taken. via: the square reached first.
uint8_t landing(uint8_t from, uint8_t steps, uint8_t *via = nullptr);

}  // namespace layout
