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

static const uint16_t PIPS[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};

// Each seat throws its own colour. On red felt the red dice and the white
// ones change seats.
void dieColours(uint8_t player, uint8_t &body, uint8_t &shade, uint8_t &pip) {
    static const uint8_t BODY[4] = {RED, BLUE, GOLD, WHITE}, SHADE[4] = {WINE, NAVY, WOOD, SILVER};
    player &= 3;
    if (pal::theme() == pal::RED_FELT && (player == 0 || player == 3)) player ^= 3;
    body = BODY[player]; shade = SHADE[player];
    pip = player >= 2 ? INK : WHITE;
}

void dieFace(int x, int y, uint8_t size, uint8_t v, uint8_t player) {
    uint8_t body, shade, pc;
    dieColours(player, body, shade, pc);
    bool big = size > 7;
    fillRound(x, y, size, size, big ? 2 : 1, body);
    if (size > 5) {                                           // a little depth
        gfx_hline(x + 2, y + size - 1, size - 4, shade);
        gfx_vline(x + size - 1, y + 2, size - 4, shade);
    }
    int step = big ? 4 : (size > 5 ? 2 : 1), dot = big ? 3 : 1, off = big ? 3 : 1;
    uint16_t m = PIPS[v <= 6 ? v : 0];
    for (uint8_t b = 0; b < 9; b++)
        if (m >> b & 1) gfx_fillRect(x + off + (b % 3) * step, y + off + (b / 3) * step, dot, dot, pc);
}

}  // namespace art
