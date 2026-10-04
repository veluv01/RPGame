// The tiles' letters: a serif face (Assets TILEFONT, tools/art/tilefont.txt
// by tools/tilefont.py) with a half-ink anti-aliasing plane and a drop
// shadow, drawn straight into the framebuffer.
#pragma once
#include <stdint.h>

// A letter (1..26) in the tiles' serif face (9 rows from its top to the
// baseline) on a tile face w pixels wide from x: a pixel left of centre:
// its anti-aliasing (the half-ink pixels) in `mid`, a tone between the
// letter's colour and the tile's, and a drop shadow a pixel down and right,
// both kept clear of the face's right bevel.
void tileLetter(int x, int y, int w, uint8_t letter, uint8_t c, uint8_t mid, uint8_t shadow);
// Capitals and spaces in the same face (menus), the half ink in mid and the
// shadow in shadow; returns the width. draw false: only measure.
int tileText(int x, int y, const char *s, uint8_t c, uint8_t mid, uint8_t shadow, bool draw = true);
