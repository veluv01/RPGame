// The table on screen, seen from above: the felt between the two racks,
// and the tiles on it. The game is played close up - the camera at twice the
// size, following the play - with the whole table a button away.
//
// World space is the whole table at its plain size - one world unit a
// screen pixel - and the view is a zoom (5 = the whole table, 10 = close up)
// about the world point the camera holds at the middle of the felt. The
// layout's 3 px units start at (X0, Y0).
//
// The tiles are drawn, not stored: bevelled bone blocks with an engraved bar
// across the middle and slate pips set in, each with a soft shadow - or,
// in a dark set, dark blocks with white pips.
// Pip size p gives a tile 3p + 4 by 6p + 7: 13 x 25 close up and on the
// rack, 10 x 19 for a hand shown at the round's end, 7 x 13 seen whole.
#pragma once
#include <stdint.h>
#include "Layout.h"

namespace table {

constexpr int CX = 64, CY = 59;                 // the middle of the felt on screen
constexpr int TOP = 19, BOT = 100;              // the felt's rows [TOP, BOT)
constexpr int X0 = 1, Y0 = 20, UNIT = 3;

extern uint8_t zoom;                            // 5..10
extern int16_t camX, camY;                      // world point at (CX, CY)

// World -> screen. (The + 320 keeps the division's operand positive, so it
// rounds the same way either side of the camera.)
inline int sx(int wx) { return ((wx - camX + 320) * zoom) / 5 - 64 * zoom + CX; }
inline int sy(int wy) { return ((wy - camY + 320) * zoom) / 5 - 64 * zoom + CY; }
inline int zscale() { return zoom * 256 / 5; }              // art scale, Q8
inline int zoomed(int px) { return px * zoom / 5; }
void setCamera(int x, int y);                   // as near (x, y) as keeps the view on the felt

// Sets of tiles (the TILES option): a palette swap of BONE and SLATE.
enum { SET_WHITE, SET_BLACK, SET_COUNT = 8 };
void useSet(uint8_t set);
uint8_t tileFace();                             // the face's colour in the set in use
uint8_t tileDim();                              // ... dimmed (a tile in hand that fits nowhere)

void drawFelt();
// A tile with its top-left at screen (x, y): pips a | b (left | right, or
// top | bottom upright), pip size p, drawn zm / 5 times the size.
// depth: how many pixels of the edge facing you show below the face.
void drawTile(int x, int y, uint8_t a, uint8_t b, bool upright, uint8_t p, uint8_t zm, uint8_t face, uint8_t edge,
              uint8_t depth = 0);
void drawBack(int x, int y, int w, int h);      // face down
// Tiles of the line, through the camera.
void placedBox(const layout::Placed &p, int &x, int &y, int &w, int &h);    // world
bool onScreen(const layout::Placed &p);
void drawPlaced(const layout::Placed &p, uint8_t face, uint8_t edge);
void drawShadow(const layout::Placed &p);
void drawScorch(const layout::Placed &p);       // what a firecracker leaves
// The shadow on the felt of a w x h tile in the air, its middle at screen
// (cx, cy) on the ground, height pixels up.
void drawLift(int cx, int cy, int w, int h, int height);
void drawGhost(const layout::Placed &p, uint8_t c, bool solid);    // where a tile would go
// The pips open at an arm's end, as a figure on a small plate just past it
// (lit: a tile in hand fits there).
void drawTag(uint8_t arm, bool lit);
// A tile in the air, turned by angle (256 a turn) about its middle at screen
// (px, py); scale (Q8) 256 is its size close up (13 x 25).
void spinTile(uint8_t a, uint8_t b, bool upright, int px, int py, uint8_t angle, int scale, const uint8_t *remap);

// A close-up tile upright as a 4 bpp image (TILE_IMG bytes, 15 clear), to
// keep and spin again and again without building it each time.
constexpr int TILE_IW = 13, TILE_IH = 25, TILE_IMG = ((TILE_IW + 1) / 2) * TILE_IH;
void tileImage(uint8_t *buf, uint8_t a, uint8_t b, bool dark);
void spinImage(const uint8_t *img, int px, int py, uint8_t angle, int scale, const uint8_t *remap);


}  // namespace table
