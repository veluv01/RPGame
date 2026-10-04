// Drawing the table (see Table.h): the wall and the dealer with his bubble,
// the board and its discs at any zoom, and the discs each side has left.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Mask and RPGfx)
// The wall, the dealer and his bubble are CHBlackjack's Table.cpp by way
// of CHRoulette's; the board is new.
#include <RPGame.h>
#include <string.h>
#include "Table.h"
#include "Fx.h"
#include "src/assets/Assets.h"

namespace table {

uint8_t zoom = 5;
int16_t camX = CX, camY = CY;

static const uint8_t RM_ID[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
// An empty hole: the disc's outline, and the dark behind the board.
static const uint8_t RM_HOLE[16] = {INK, NAVY, NAVY, NAVY, NAVY, NAVY, NAVY, NAVY,
                                    NAVY, NAVY, NAVY, NAVY, NAVY, NAVY, NAVY, NAVY};
// One of the winning four: its face in the rainbow.
static const uint8_t RM_LIT[16] = {INK, FX_A, 2, 3, 4, WHITE, 6, 7, 8, 9, WHITE, 11, WHITE, 13, 14, 15};

void setCamera(int x, int y) {
    // The view is 640 / zoom world units across: keep it on the screen's world.
    int half = 320 / zoom;
    if (x < half) x = half;
    if (x > 128 - half) x = 128 - half;
    if (y < half) y = half;
    if (y > 128 - half) y = 128 - half;
    camX = (int16_t)x; camY = (int16_t)y;
}

void wall() {
    // Pinstripe wallpaper: build one row, copy it down the wall (vertical
    // lines drawn pixel by pixel cost over a millisecond). gfx_copyRow copies
    // words from SRAM when both rows are word aligned; memcpy is a byte loop.
    uint8_t row[GFX_FB_STRIDE] __attribute__((aligned(4)));
    memset(row, NAVY | (NAVY << 4), sizeof row);
    for (int x = 3; x < 128; x += 8) row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (INK << 4));
    for (int y = 0; y < WALL_H; y++) gfx_copyRow(y, row, 0, GFX_W);
    dither(0, 0, 128, 3, INK, 0);                       // darker ceiling
    dither(DEALER_X + 6, 2, 36, 30, WOOD, 1);           // warm spotlight behind the dealer
}

void rail(uint32_t frame) {
    gfx_hline(0, RAIL_Y, 128, GOLD);
    gfx_hline(0, RAIL_Y + 1, 128, WOOD);
    // A marquee: every third bulb lit, the light running left to right.
    gfx_hline(0, RAIL_Y + 2, 128, INK);
    uint32_t k = frame >> 3;
    for (int i = 0; i < 32; i++) gfx_pixel(1 + i * 4, RAIL_Y + 2, (i + 3 - k % 3) % 3 ? WOOD : FX_B);
}

void dealer(uint8_t expr, uint8_t look, int x, int y, bool big) {
    int k = big ? 2 : 1, scale = big ? 512 : 256;
    sprite4(DEALER, x, y, RM_ID, scale);
    int fx = x + 12 * k, fy = y + 14 * k;
    sprite4(FACE_NORMAL, fx, fy, RM_ID, scale);
    if (expr > E_TALK) expr = E_NORMAL;
    if (expr) {
        for (uint16_t i = FACE_EDIT_AT[expr - 1]; i < FACE_EDIT_AT[expr]; i++) {
            uint16_t w = FACE_EDITS[i];
            uint16_t idx = w >> 4;
            if (big) gfx_fillRect(fx + (idx % 24) * 2, fy + (idx / 24) * 2, 2, 2, (uint8_t)(w & 15));
            else gfx_pixel(fx + idx % 24, fy + idx / 24, (uint8_t)(w & 15));
        }
    }
    // Pupils glance toward whatever is moving.
    if (expr != E_BLINK && look != 1) {
        int dx = look == 0 ? -1 : 1;
        for (int e = 0; e < 2; e++) {
            int ex = fx + (e ? 15 : 5) * k;              // sclera, 4 px wide
            gfx_fillRect(ex, fy + 6 * k, 4 * k, 2 * k, WHITE);
            gfx_fillRect(ex + (1 + dx) * k, fy + 6 * k, 2 * k, 2 * k, INK);
        }
    }
}

int textRows(const char *s) {
    int n = 1;
    for (; *s; s++) n += *s == '\n';
    return n;
}

void typedText(int cx, int y, const char *src, int typed, uint8_t colour) {
    for (const char *p = src; *p;) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        char line[20];
        int n = len < 19 ? len : 19;
        memcpy(line, p, n);
        line[n] = 0;
        int lx = cx - text35Width(line) / 2;            // centred on its full width
        int show = typed < n ? (typed > 0 ? typed : 0) : n;
        line[show] = 0;
        text35(lx, y, line, colour);
        typed -= len + 1;
        y += 7;
        if (!e) break;
        p = e + 1;
    }
}

void speechBubble(const char *src, int typed) {
    int x = BUBBLE_X, y = BUBBLE_Y, w = BUBBLE_W, h = BUBBLE_H;
    panel(x, y, w, h, 4, WHITE, INK);
    // Tail toward the dealer's mouth.
    for (int i = 0; i < 5; i++) {
        gfx_hline(x - 5 + i, y + 22 + i, 6 - i, WHITE);
        gfx_pixel(x - 6 + i, y + 22 + i, INK);
    }
    gfx_vline(x, y + 21, 4, WHITE);
    typedText(x + w / 2, y + h / 2 - (textRows(src) * 7) / 2 + 1, src, typed, INK);
}

// A disc-shaped sprite at world (x, y): the plain art at the plain size, the
// big art close up, and the big art scaled on the way between.
static void disc(int x, int y, const uint8_t *remap) {
    int X = sx(x), Y = sy(y);
    if (X > 127 || Y > 127 || X < -24 || Y < -24) return;
    if (zoom == 5) sprite4(DISC, X, Y, remap);
    else sprite4(DISC_BIG, X, Y, remap, zoom == 10 ? 256 : zoom * 256 / 10);
}

void drawDisc(int x, int y, uint8_t side, bool lit) {
    if (!lit) { disc(x, y, DISC_REMAP[side]); return; }
    // The rainbow face keeps the side's rim.
    uint8_t rm[16];
    memcpy(rm, RM_LIT, 16);
    rm[5] = DISC_REMAP[side][5];
    disc(x, y, rm);
}

void drawBoard(const c4::Board &b, c4::Bits lit) {
    // The felt, with the board's shadow on it; then the board: its legs, its
    // face, a lighter top edge.
    int top = zoom == 5 ? RAIL_Y + 2 : 0;
    int x0 = sx(BOARD_X), x1 = sx(BOARD_X + c4::COLS * CELL), y0 = sy(BOARD_Y), y1 = sy(BOARD_Y + c4::ROWS * CELL);
    int leg = x0 - sx(BOARD_X - 2);
    // (Close up the board is most of the screen: only the felt that shows
    // is filled. The board stands on the bottom edge at any zoom.)
    gfx_fillRect(0, top, 128, y0 - top, FELT);
    gfx_fillRect(0, y0, x0 - leg, 128 - y0, FELT);
    gfx_fillRect(x1 + leg, y0, 128 - x1 - leg, 128 - y0, FELT);
    if (zoom == 5) {
        // The table under the lamp: every green there is, in rings from the
        // light in the middle out to the dark corners. (The second dither
        // puts FELT where the first left FELT, so it only shows in the
        // light ring.)
        dither(0, top, 128, 128 - top, FELT_DK, 0);
        gfx_fillEllipse(64, 100, 72, 62, FELT);
        gfx_fillEllipse(64, 100, 65, 56, FELT_LT);
        dither(0, top, 128, 128 - top, FELT, 1);
        gfx_fillEllipse(64, 100, 57, 49, FELT_LT);
        gfx_fillRect(0, top, 128, 1, FELT_DK);
        dither(x0 - leg - 2, y0 + 3, 2, y1 - y0 - 3, FELT_DK, 0);
        dither(x1 + leg, y0 + 3, 2, y1 - y0 - 3, FELT_DK, 0);
    }
    gfx_fillRect(x0 - leg, y0 - leg / 2, leg, y1 - y0 + leg / 2, NAVY);
    gfx_fillRect(x1, y0 - leg / 2, leg, y1 - y0 + leg / 2, NAVY);
    gfx_fillRect(x0, y0, x1 - x0, y1 - y0, BLUE);
    gfx_hline(x0, y0, x1 - x0, CYAN);
    for (uint8_t c = 0; c < c4::COLS; c++)
        for (uint8_t r = 0; r < c4::ROWS; r++) {
            c4::Bits bit = c4::cell(c, r);
            int x = discX(c), y = discY(r);
            if (b.side[0] & bit) drawDisc(x, y, c4::RED, (lit & bit) != 0);
            else if (b.side[1] & bit) drawDisc(x, y, c4::GOLD, (lit & bit) != 0);
            else disc(x, y, RM_HOLE);
        }
}

void frameOver(uint8_t col, int y0, int y1) {
    // (Only ever at the plain size: discs fall with the camera back.)
    int x = BOARD_X + col * CELL;
    for (int r = 0; r < c4::ROWS; r++) {
        int y = BOARD_Y + r * CELL;
        if (y + CELL <= y0 || y > y1) continue;
        sprite4(CELL_FRAME, x, y, RM_ID);
        if (!r) gfx_hline(x, y, CELL, CYAN);
    }
}

void drawStacks(uint8_t red, uint8_t gold) {
    // Chips seen edge-on, two rows each, standing on the bottom of the screen.
    for (uint8_t s = 0; s < 2; s++) {
        int x = s ? 113 : 5, n = s ? gold : red;
        for (int i = 0; i < n; i++) {
            int y = 126 - i * 2 - (i / 7);              // a gap every seven: easier to count
            gfx_hline(x, y, DISC_PX, DISC_REMAP[s][1]);
            gfx_hline(x, y + 1, DISC_PX, DISC_REMAP[s][5]);
            gfx_pixel(x + 2, y, WHITE);
        }
    }
}

}  // namespace table
