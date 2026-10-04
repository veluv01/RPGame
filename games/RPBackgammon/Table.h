// The board on screen, seen from above: a wooden frame round two fields of
// felt, twelve points printed on each long side, the bar between with the
// doubling cube, and a tray for the checkers borne off. The camera can come
// in to twice the size on any part of it.
//
// World space is the board at its plain size - one world unit a screen
// pixel, the origin the screen's - and the view is a zoom (5 = plain, up to
// 10 = doubled) about the world point the camera holds at the middle of the
// screen. White's home board is bottom right, Red's top right (or both on
// the left: mirror); a side's points are in its own numbering (bg::BAR,
// bg::OFF) unless a name says "White's".
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace table {

constexpr int CX = 64, CY = 64;                 // the middle of the view (rows 10..117)
constexpr int TOP = 10, BOT = 118;              // the board's rows [TOP, BOT)
constexpr int FIELD_T = 13, FIELD_B = 115;      // the felt's
constexpr int LEFT_X = 2, RIGHT_X = 64;         // the two halves' first columns (before mirroring)
constexpr int PW = 9, PH = 42;                  // a point: its base and its length
constexpr int BAR_X = 56, TRAY_X = 119, SLOT_H = 45;
constexpr int CHIP = 8, PITCH = 7;              // a checker, and a stack's step (the outlines overlap)
constexpr int DIE = 12, DICE_Y = 58;
constexpr int CUBE = 10;

extern uint8_t zoom;                            // 5..10
extern int16_t camX, camY;                      // world point at (CX, CY)
extern bool mirror;                             // the home boards on the left (the HOME option)

// World -> screen. (The + 320 keeps the division's operand positive, so it
// rounds the same way either side of the camera.)
inline int sx(int wx) { return ((wx - camX + 320) * zoom) / 5 - 64 * zoom + CX; }
inline int sy(int wy) { return ((wy - camY + 320) * zoom) / 5 - 64 * zoom + CY; }
inline int zscale() { return zoom * 256 / 5; }              // art scale, Q8
inline int zoomed(int px) { return px * zoom / 5; }
void setCamera(int x, int y);                   // as near (x, y) as keeps the view on the board
// The left edge of a box w wide at x, with the board mirrored or not.
inline int mx(int x, int w) { return mirror ? 128 - x - w : x; }

// Where things are (world, top-left corners).
inline uint8_t whites(uint8_t side, uint8_t p) { return side == bg::WHITE ? p : (uint8_t)(25 - p); }
int pointX(uint8_t whitePoint);                 // 1..24: its left column
// Checker k (0 = first down) of n on the side's point p: a point, the bar,
// or the tray (bg::OFF: an 8 x 2 chip on its edge).
void checkerAt(uint8_t side, uint8_t p, uint8_t k, uint8_t n, int &x, int &y);
void dieAt(uint8_t side, uint8_t i, uint8_t n, int &x, int &y);    // die i of n in front of the side
void cubeAt(uint8_t owner, int &x, int &y);    // the cube: owned by a side, or 2 = in the middle

void drawBoard(int y0 = 0, int y1 = 128);       // the rows of the screen to draw (the HUD covers the rest)
// Mark where a checker may go: half of the pixels of the side's point p (or
// of its tray) in c, or all of them.
void tint(uint8_t side, uint8_t p, uint8_t c, bool solid);
// A checker with its top-left at world (x, y); lift > 0 raises it off the
// felt (nearer the eye: bigger, over its shadow).
void drawChecker(int x, int y, const uint8_t *remap, uint8_t lift = 0);
void drawOff(uint8_t side, uint8_t k);          // chip k standing in the side's tray
// A die, its top-left at world (x, y); turn != 0 spins it about its middle,
// and lift raises it like a checker.
void drawDie(int x, int y, uint8_t face, const uint8_t *remap, uint8_t turn = 0, uint8_t lift = 0);
// The doubling cube showing value, face colour `face`, raised by lift.
void drawCube(int x, int y, uint16_t value, uint8_t face, uint8_t lift = 0);

}  // namespace table
