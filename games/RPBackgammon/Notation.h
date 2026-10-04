// A play in the notation backgammon players use: "24/18 13/11", "8/5*
// 6/5*" (a hit), "13/5" (one checker moving on), "BAR/22", "6/OFF",
// "13/9(2)" (two checkers the same way). Points in the mover's numbering.
#pragma once
#include <stdint.h>

// Writes at most 40 characters and the terminating zero; returns out.
char *notate(char *out, const uint8_t *from, const uint8_t *die, const uint8_t *hit, uint8_t n);
