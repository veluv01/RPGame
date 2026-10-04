#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Chips derived from CHBlackjack's (its CardArt.cpp), which grew out of
// Press-Play-On-Tape/Blackjack (Apache-2.0); the rest is new.
#include <RPGame.h>
#include "Chips.h"
#include "src/assets/Assets.h"

namespace art {

const uint16_t CHIP_VALUE[DENOMS] = {1, 5, 25, 100};
static const uint8_t CHIP_BODY[DENOMS] = {WHITE, RED, FELT_LT, INK};
static const uint8_t CHIP_EDGE[DENOMS] = {BLUE, WHITE, WHITE, GOLD};
static const uint8_t CHIP_SHADE[DENOMS] = {SILVER, WINE, FELT_DK, INK};

int chipDenom(int32_t amount) {
    for (int i = DENOMS - 1; i >= 0; i--) if (amount >= CHIP_VALUE[i]) return i;
    return 0;
}

// The chip sprites are drawn in placeholder colours - WHITE body, BLUE
// edge marks, SILVER shade - which the denomination's remap replaces.
static void chipSprite(const uint8_t *spr, int x, int y, uint8_t d) {
    uint8_t rm[16];
    for (uint8_t i = 0; i < 16; i++) rm[i] = i;
    rm[WHITE] = CHIP_BODY[d]; rm[BLUE] = CHIP_EDGE[d]; rm[SILVER] = CHIP_SHADE[d];
    sprite4(spr, x, y, rm);
}

void chip(int cx, int y, uint8_t d, bool top) {
    if (top) chipSprite(CHIP_TOP, cx - 7, y - 1, d);
    else chipSprite(CHIP_SIDE, cx - 7, y + 1, d);
}

void chipSmall(int cx, int y, uint8_t d, bool top) {
    if (top) chipSprite(CHIP_SMALL_TOP, cx - 5, y - 1, d);
    else chipSprite(CHIP_SMALL_SIDE, cx - 5, y + 2, d);
}

// Chips for an amount, largest first; the top of a tall stack is shown.
static uint8_t breakdown(int32_t amount, uint8_t *chips) {
    uint8_t n = 0;
    for (int d = DENOMS - 1; d >= 0 && n < 24; d--)
        while (amount >= CHIP_VALUE[d] && n < 24) { chips[n++] = (uint8_t)d; amount -= CHIP_VALUE[d]; }
    return n;
}

void stackSmall(int cx, int baseY, int32_t amount, uint8_t maxChips) {
    uint8_t chips[24], n = breakdown(amount, chips);
    uint8_t first = n > maxChips ? (uint8_t)(n - maxChips) : 0;
    for (uint8_t i = first; i < n; i++) chipSmall(cx, baseY - 2 * (i - first), chips[i], i == n - 1);
}

void stack(int cx, int baseY, int32_t amount, uint8_t maxChips) {
    uint8_t chips[24], n = breakdown(amount, chips);
    uint8_t first = n > maxChips ? (uint8_t)(n - maxChips) : 0;
    for (uint8_t i = first; i < n; i++) chip(cx, baseY - 2 * (i - first), chips[i], i == n - 1);
}

void puck(int cx, int cy, bool on, uint8_t number) {
    if (!on) {
        sprite4(PUCK_OFF, cx - 6, cy - 6);
        text35(cx - 5, cy - 2, "OFF", WHITE);
        return;
    }
    sprite4(PUCK_ON, cx - 5, cy - 5);
    if (!number) { text35(cx - 3, cy - 2, "ON", INK); return; }
    // The point in the 3x5 font, centred (odd widths in an odd disc): small
    // print, so the badge reads apart from the layout's own numbers.
    char s[3] = {(char)(number >= 10 ? '1' : '0' + number), (char)(number >= 10 ? '0' : 0), 0};
    text35(cx - text35Width(s) / 2, cy - 2, s, INK);
}

static const uint16_t PIPS[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};

void dieFace(int x, int y, uint8_t size, uint8_t v) {
    fillRound(x, y, size, size, 1, RED);
    if (size > 5) {                                           // a little depth
        gfx_hline(x + 1, y + size - 1, size - 2, WINE);
        gfx_vline(x + size - 1, y + 1, size - 2, WINE);
    }
    int step = size > 5 ? 2 : 1;
    uint16_t m = PIPS[v <= 6 ? v : 0];
    for (uint8_t b = 0; b < 9; b++)
        if (m >> b & 1) gfx_pixel(x + 1 + (b % 3) * step, y + 1 + (b / 3) * step, WHITE);
}

}  // namespace art
