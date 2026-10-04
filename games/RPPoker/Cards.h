// Cards: 0..51 = rank * 4 + suit.
//
//   rank 0..12 = 2, 3, ... 10, J, Q, K, A
//   suit 0..3  = clubs, diamonds, hearts, spades (stud's bring-in order)
//
// So "lower card" is simply the smaller number, the evaluator reads rank and
// suit with a shift and a mask, and the card art is drawn in this order.
#pragma once
#include <stdint.h>

static inline uint8_t rankOf(uint8_t c) { return (uint8_t)(c >> 2); }
static inline uint8_t suitOf(uint8_t c) { return (uint8_t)(c & 3); }
static inline bool    redCard(uint8_t c) { uint8_t s = (uint8_t)(c & 3); return s == 1 || s == 2; }
static inline uint8_t makeCard(uint8_t rank, uint8_t suit) { return (uint8_t)(rank * 4 + suit); }

enum : uint8_t { R2 = 0, R3, R4, R5, R6, R7, R8, R9, RT, RJ, RQ, RK, RA };
enum : uint8_t { CLUBS = 0, DIAMONDS, HEARTS, SPADES };
constexpr uint8_t NO_CARD = 0xFF;
