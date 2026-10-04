// The board on screen, seen from above: ten rows of ten squares in a wooden
// frame, the ladders lying across it and the snakes wriggling on top. The
// camera can come in to twice the size on any part of it (CHBackgammon's
// table, with its zoom).
//
// World space is the board at its plain size - one world unit a screen
// pixel, the origin the screen's - and the view is a zoom (5 = plain, the
// whole board; up to 10 = doubled) about the world point the camera holds at
// the middle of the view.
#pragma once
#include <stdint.h>

namespace board {

constexpr int CX = 64, CY = 69;                 // the middle of the view (rows 10..127, under the HUD)
constexpr int TOP = 10;
constexpr int CELL = 11, X0 = 9, Y0 = 14;       // a square; the grid's top-left

extern uint8_t zoom;                            // 5..10
extern int16_t camX, camY;                      // world point at (CX, CY)
extern uint8_t light, dark;                     // the squares' two tones
extern bool numbers;                            // print the numbers at the plain size too

// World -> screen. (The + 320 keeps the division's operand positive, so it
// rounds the same way either side of the camera.)
inline int sx(int wx) { return ((wx - camX + 320) * zoom) / 5 - 64 * zoom + CX; }
inline int sy(int wy) { return ((wy - camY + 320) * zoom) / 5 - 64 * zoom + CY; }
inline int zoomed(int px) { return px * zoom / 5; }
// Art is drawn at whole sizes only: as it is, or doubled close up.
inline bool big() { return zoom >= 8; }
inline int zscale() { return big() ? 512 : 256; }
inline int sized(int px) { return big() ? 2 * px : px; }
// As near (x, y) as keeps the view on the board; `below`: rows of the screen
// covered at the bottom (a bar), which the board may scroll up behind.
void setCamera(int x, int y, int below = 0);

// The middle of square n (1..100), world.
void centre(uint8_t n, int &x, int &y);

void drawBoard();                               // the frame and the squares (every row under the HUD)
void mark(uint8_t n, uint8_t c);                // a frame round square n
// Ladder i (layout::LINK), its first `lit` rungs (of 256ths of its length) alight.
void drawLadder(uint8_t i, int lit);

// Snake i (layout::LINK[LADDERS + i]) as it lies now.
struct Pose {
    uint8_t phase;                              // of its wriggle
    uint8_t amp;                                // ... and how wide (world, Q4)
    bool open;                                  // jaws
    int16_t bulge;                              // a meal on its way down, 0..256 along the body (-1: none)
    uint8_t bulgeColour;
};
void drawSnake(uint8_t i, const Pose &p, uint32_t frame);
// A snake of `beads` beads in screen space (Q4), head first: the title's own.
void chain(int hx, int hy, int tx, int ty, bool large, int beads, int amp16, uint8_t phase, uint8_t c1, uint8_t c2,
           const Pose &p, uint32_t frame);

}  // namespace board
