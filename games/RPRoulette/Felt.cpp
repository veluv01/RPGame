#pragma GCC optimize("Os")   // cold code: size over speed
#include <RPGame.h>
#include <string.h>
#include "Felt.h"
#include "Layout.h"
#include "Wheel.h"

namespace felt {

// The layout's rows are a few patterns (a number row, the dozens, the even
// money), each built once into a row buffer and stamped down its rows with
// gfx_copyRow at word speed: the cells' fills and the gold verticals in one
// go, instead of a fill per cell and a vertical line drawn pixel by pixel.
static __attribute__((noinline)) void put(uint8_t *row, int x, uint8_t c) {
    if ((unsigned)x >= GFX_W) return;
    uint8_t &b = row[x >> 1];
    b = (x & 1) ? (uint8_t)((b & 0x0F) | (c << 4)) : (uint8_t)((b & 0xF0) | c);
}

static __attribute__((noinline)) void span(uint8_t *row, int x0, int x1, uint8_t c, int ox) {
    for (int x = x0; x <= x1; x++) put(row, x + ox, c);
}

static void stamp(const uint8_t *row, int y0, int n) {
    for (int y = y0; y < y0 + n; y++) gfx_copyRow(y, row, 0, GFX_W);
}

// Numbers fill their cells, RED or INK with WHITE digits (red digits on the
// felt would be as bright as it is, and unreadable); the zero is green.
//   column c (1..12) inside x 9c..9c+7, rows top 49..57, middle 59..67,
//   bottom 69..77; the zero column x 1..7; the 2 TO 1 cells x 117..126;
//   gold lines at x 0, 8 + 9k and 127, and at y 48, 58, 68, 78, 88, 98.
void draw(bool us, int ox) {
    uint8_t row[GFX_FB_STRIDE] __attribute__((aligned(4)));
    gfx_fillRect(0, lay::TABLE_Y, 128, lay::BAR_Y - lay::TABLE_Y, FELT);
    gfx_hline(0, lay::TABLE_Y, 128, FELT_DK);
    for (int r = 0; r < 3; r++) {                        // the number rows
        memset(row, FELT | (FELT << 4), sizeof row);
        span(row, 0, 127, GOLD, ox);
        span(row, 1, 7, FELT_LT, ox);                     // the zero column
        span(row, 117, 126, FELT, ox);                    // 2 TO 1
        for (int c = 1; c <= 12; c++)
            span(row, 9 * c, 9 * c + 7, wheel::colour((uint8_t)(3 * (c - 1) + r + 1)) == wheel::RED_NUM ? RED : INK, ox);
        stamp(row, 69 - 10 * r, 9);
    }
    memset(row, FELT | (FELT << 4), sizeof row);         // the dozens, then the even money
    for (int x = 8; x <= 116; x += 36) put(row, x + ox, GOLD);
    stamp(row, 79, 9);
    for (int x = 26; x <= 98; x += 36) put(row, x + ox, GOLD);
    stamp(row, 89, 9);
    gfx_hline(ox, 48, 128, GOLD);
    gfx_hline(ox, 78, 128, GOLD);
    for (int y = 58; y <= 68; y += 10) {                 // the rows between: the zero goes on through
        gfx_hline(ox, y, 128, GOLD);
        gfx_hline(1 + ox, y, 7, FELT_LT);
    }
    gfx_hline(8 + ox, 88, 109, GOLD);
    gfx_hline(8 + ox, 98, 109, GOLD);
    if (us) gfx_hline(ox, 63, 9, GOLD);                  // 00 above, 0 below

    char s[3];
    for (uint8_t n = 1; n <= 36; n++) {
        int c = (n - 1) / 3 + 1, r = (n - 1) % 3;
        *wheel::name(s, n) = 0;
        text35(9 * c + ox + (n < 10 ? 3 : 1), 71 - 10 * r, s, WHITE);
    }
    if (us) { text35(1 + ox, 53, "00", INK); text35(3 + ox, 68, "0", INK); }
    else text35(3 + ox, 61, "0", INK);
    // 2 TO 1, kerned into the 10 px cells: "2", a colon, "1".
    for (int k = 0; k < 3; k++) {
        int y = 71 - 10 * k;
        text35(118 + ox, y, "2", WHITE);
        gfx_pixel(122 + ox, y + 1, WHITE);
        gfx_pixel(122 + ox, y + 3, WHITE);
        text35(124 + ox, y, "1", WHITE);
    }
    static const char *const DOZ[3] = {"1st 12", "2nd 12", "3rd 12"};
    for (int k = 0; k < 3; k++) text35(15 + 36 * k + ox, 81, DOZ[k], WHITE);
    text35(10 + ox, 91, "1-18", WHITE);
    text35(28 + ox, 91, "EVEN", WHITE);
    text35(84 + ox, 91, "ODD", WHITE);
    text35(99 + ox, 91, "19", WHITE);                    // "19-36" kerned into 17 px
    gfx_hline(107 + ox, 93, 2, WHITE);
    text35(109 + ox, 91, "36", WHITE);
    // The red and black diamonds.
    static const uint8_t HW[5] = {1, 3, 5, 3, 1};
    for (int d = 0; d < 2; d++) {
        int cx = (d ? 71 : 53) + ox;
        for (int i = 0; i < 5; i++) {
            gfx_hline(cx - HW[i] + 1, 91 + i, 2 * HW[i] - 1, d ? INK : RED);
            gfx_pixel(cx - HW[i], 91 + i, GOLD);
            gfx_pixel(cx + HW[i], 91 + i, GOLD);
        }
        gfx_pixel(cx, 90, GOLD);
        gfx_pixel(cx, 96, GOLD);
    }
    gfx_hline(0, lay::BAR_Y - 1, 128, FELT_DK);
}

void ring(int x0, int y0, int x1, int y1, uint8_t c, int ox) {
    x0 += ox; x1 += ox;
    gfx_hline(x0 - 1, y0 - 1, x1 - x0 + 3, c);
    gfx_hline(x0 - 1, y1 + 1, x1 - x0 + 3, c);
    gfx_hline(x0, y0, x1 - x0 + 1, c);
    gfx_hline(x0, y1, x1 - x0 + 1, c);
    gfx_vline(x0 - 1, y0, y1 - y0 + 1, c);
    gfx_vline(x1 + 1, y0, y1 - y0 + 1, c);
}

}  // namespace felt
