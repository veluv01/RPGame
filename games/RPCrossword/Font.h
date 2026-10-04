// The display font (tools/art/font.txt: bold slab serifs, capitals 11 px,
// lowercase descending to 13), drawn through the library's 1 bpp masks
// (Mask.h) so it can be outlined, shadowed and graded like the 3x5 font.
#pragma once
#include <RPGame.h>

// The text with its top row at y in a mask. dy, if given, moves each
// character up or down (dancing banners); gap is the space between letters
// (their outlines touch at 1, as in CHBlackjack's lettering).
void maskFont(Mask &m, int x, int y, const char *s, const int8_t *dy = nullptr, uint8_t gap = 1);
int  fontWidth(const char *s, uint8_t gap = 1);
constexpr int FONT_H = 13;
// Plain lettering in it, one colour, its top row at y (menus, values);
// returns the width. Render time only (the mask).
int fontText(int x, int y, const char *s, uint8_t c, uint8_t gap = 1);
