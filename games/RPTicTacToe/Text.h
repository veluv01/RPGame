// The words: table names, the rules as the dealer tells them, his remarks.
#pragma once
#include "Rules.h"

extern const char *const MODE_NAME[MODE_COUNT];
extern const char *const MODE_RULES[MODE_COUNT];    // up to 5 lines of 17 characters

constexpr uint8_t QUIP_CENTRE = 0, QUIP_CORNER = 1, QUIP_EDGE = 2, QUIP_DARK = 3,
                  QUIP_OVER = 4,            // + Result - 1, two of each
                  QUIP_ANY = 10, QUIP_ANY_N = 6;
extern const char *const QUIPS[QUIP_ANY + QUIP_ANY_N];
