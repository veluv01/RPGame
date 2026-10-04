// The serif lettering (tools/art/aafont.txt: CHCrossword's DejaVu Serif Bold
// at 12 px, capitals 9 px, with figures and a few stops), anti-aliased as far
// as sixteen colours go: each glyph has its ink and a layer of half ink on
// its curves and diagonals, drawn in a tone between the ink and what is
// under it. The ink goes through the library's 1 bpp masks (Mask.h) so it
// can be outlined, shadowed and graded; both layers are glyph16 rows.
#pragma once
#include <RPGame.h>
#include "src/assets/Assets.h"

// The ink into a mask, its capitals' top at y: dy, if given, moves each
// character up or down (dancing banners); gap is the space between letters.
void maskFont(Mask &m, int x, int y, const char *s, const int8_t *dy = nullptr, uint8_t gap = 1);
// ... and the half ink to go with it, straight onto the screen, the mask's
// (0, 0) at (x, y) as maskDraw puts it.
void fontHalf(int x, int y, const char *s, const int8_t *dy, uint8_t gap, uint8_t c);
int  fontWidth(const char *s, uint8_t gap = 1);
constexpr int FONT_H = AAFONT_H;
// Plain lettering, its capitals' top at y: the half ink in a tone between c
// and what it is usually on (white: silver, gold: wood, ...); returns the
// width.
int fontText(int x, int y, const char *s, uint8_t c, uint8_t gap = 1);
