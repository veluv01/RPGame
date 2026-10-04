// What is on the table: the back wall with the dealer (CHBlackjack's, and his
// speech bubble), the rail, and below it the board standing on the felt -
// seven columns of six holes, the discs behind them.
//
// World space is the screen at its plain size, one unit a pixel, and the
// view a zoom (5 = plain, up to 10 = doubled) about the world point the
// camera holds at the middle of the screen: CHBackgammon's arrangement, used
// here to come in close on the winning four. The wall is only drawn at the
// plain size.
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace table {

enum Expr : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

constexpr int WALL_H = 42;
constexpr int DEALER_X = 2, DEALER_Y = 0;          // even x: fast blits
constexpr int DEALER_W = 48, DEALER_H = 42;
constexpr int PLAQUE_X = 52, PLAQUE_Y = 3, PLAQUE_W = 48, PLAQUE_H = 34;
constexpr int BUBBLE_X = 50, BUBBLE_Y = 2, BUBBLE_W = 51, BUBBLE_H = 35;   // over the plaque
constexpr int TURN_X = 103, TURN_Y = 3, TURN_W = 23, TURN_H = 34;
constexpr int RAIL_Y = 42;                         // two rows
constexpr int LANE_Y = 45;                         // where a disc hovers before it is dropped
constexpr int CELL = 12, DISC_PX = 10;
constexpr int BOARD_X = 22, BOARD_Y = 56;          // 84 x 72

constexpr int CX = 64, CY = 64;
extern uint8_t zoom;                               // 5..10
extern int16_t camX, camY;                         // world point at (CX, CY)

// World -> screen. (The + 320 keeps the division's operand positive, so it
// rounds the same way either side of the camera.)
inline int sx(int wx) { return ((wx - camX + 320) * zoom) / 5 - 64 * zoom + CX; }
inline int sy(int wy) { return ((wy - camY + 320) * zoom) / 5 - 64 * zoom + CY; }
void setCamera(int x, int y);                      // as near (x, y) as keeps the view on the screen's world

// A cell's disc, top-left, in world space.
inline int discX(uint8_t col) { return BOARD_X + col * CELL + 1; }
inline int discY(uint8_t row) { return BOARD_Y + (c4::ROWS - 1 - row) * CELL + 1; }

void wall();
void rail(uint32_t frame);                         // ... with a row of bulbs chasing along it
// The dealer with his top-left at (x, y); look: 0 left, 1 centre, 2 right.
// big: at twice the size (the endings).
void dealer(uint8_t expr, uint8_t look, int x = DEALER_X, int y = DEALER_Y, bool big = false);
// Lines of 3x5 text centred on cx, the block's first row at y, of which the
// first `typed` characters are drawn (the typewriter).
void typedText(int cx, int y, const char *text, int typed, uint8_t colour);
int textRows(const char *text);
void speechBubble(const char *text, int typed);

// The felt, the board and the discs at rest, through the camera. lit: cells
// (c4 bits) drawn in the rainbow.
void drawBoard(const c4::Board &b, c4::Bits lit);
// A disc of the side with its top-left at world (x, y).
void drawDisc(int x, int y, uint8_t side, bool lit = false);
// The board's face over rows of a column (a disc falling behind it shows
// only through the holes): the cells from world row y0 to y1.
void frameOver(uint8_t col, int y0, int y1);
// Each side's discs still to play, in stacks either side of the board: 21
// each to begin with, one fewer for every disc played (and the one in hand).
void drawStacks(uint8_t red, uint8_t gold);

}  // namespace table
