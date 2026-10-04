#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The board view (BoardView.h): category strip, puzzle board, podiums, prompt
// bar and letter picker. The presenter decides when each part is redrawn.
#include <RPGame.h>
#include <string.h>
#include "BoardView.h"
#include "Layout.h"
#include "Puzzle.h"
#include "Shapes.h"

namespace board {

using namespace lay;

static const uint8_t POD_C[3] = {RED, GOLD, BLUE}, POD_D[3] = {WINE, WOOD, NAVY};
static const uint8_t POD_L[3] = {SKIN, WHITE, CYAN};                     // where the light catches

static void bold57(int x, int y, const char *s, uint8_t c) {
    gfx_text(x, y, s, c);                               // double-struck = bold
    gfx_text(x + 1, y, s, c);
}

// Lettering that stands off what it is printed on.
static void raised57(int x, int y, const char *s, uint8_t c) {
    bold57(x + 1, y + 1, s, INK);
    bold57(x, y, s, c);
}

void strip(const char *category) {
    gfx_fillRect(0, STRIP_Y, 128, STRIP_H, NAVY);
    gfx_hline(0, STRIP_Y, 128, GOLD);
    gfx_hline(0, STRIP_Y + 1, 128, BLUE);               // the sign's lit top edge
    gfx_hline(0, STRIP_Y + STRIP_H - 1, 128, GOLD);
    int x = 64 - text35Width(category) / 2;
    text35(x + 1, STRIP_Y + 3, category, INK);
    text35(x, STRIP_Y + 2, category, WHITE);
}

void frame() {
    gfx_fillRect(0, BOARD_Y, 128, BOARD_H, INK);
    bevel(0, BOARD_Y, 128, BOARD_H, FX_B, WOOD);        // a gold frame, its top edge glinting
}

void cell(uint8_t i, uint8_t look, char ch, bool cursor) {
    int x = PANEL_X0 + (i % pz::COLS) * PITCH_X, y = PANEL_Y0 + (i / pz::COLS) * PITCH_Y;
    switch (look) {
        case NONE:
            // The board's four corners: a light bulb each, in the cycling colour.
            gfx_fillRect(x, y, PANEL_W, PANEL_H, INK);
            fillRound(x + 1, y + 2, 6, 6, 2, FX_A);
            gfx_pixel(x + 2, y + 3, WHITE);
            gfx_hline(x + 2, y + 8, 4, WOOD);
            return;
        case EMPTY:
            // Sunk into the board: in shadow under its top and left edges.
            gfx_fillRect(x, y, PANEL_W, PANEL_H, FELT);
            dither(x, y, PANEL_W, PANEL_H, FELT_DK, (uint8_t)((i + i / pz::COLS) & 1));
            gfx_hline(x, y, PANEL_W, FELT_DK);
            gfx_vline(x, y + 1, PANEL_H - 1, FELT_DK);
            return;
        case LIT:
            gfx_fillRect(x, y, PANEL_W, PANEL_H, CYAN);
            bevel(x, y, PANEL_W, PANEL_H, WHITE, BLUE);
            return;
        case TURN1: case TURN2: {
            // Turning: the panel seen edge-on.
            int w = look == TURN1 ? 4 : 2;
            gfx_fillRect(x, y, PANEL_W, PANEL_H, INK);
            gfx_fillRect(x + (PANEL_W - w) / 2, y, w, PANEL_H, look == TURN1 ? CYAN : WHITE);
            return;
        }
        default: break;
    }
    // A raised tile, lit from the top left.
    gfx_fillRect(x, y, PANEL_W, PANEL_H, WHITE);
    gfx_hline(x + 1, y + PANEL_H - 1, PANEL_W - 1, SILVER);
    gfx_vline(x + PANEL_W - 1, y, PANEL_H - 1, SILVER);
    if (cursor) gfx_rect(x, y, PANEL_W, PANEL_H, RED);
    if (look == BLANK || !ch) return;
    char s[2] = {ch, 0};
    bold57(x + 1, y + 2, s, look == GUESS ? BLUE : INK);
}

void cursorBox(uint8_t i) {
    int x = PANEL_X0 + (i % pz::COLS) * PITCH_X, y = PANEL_Y0 + (i / pz::COLS) * PITCH_Y;
    gfx_rect(x - 1, y - 1, PANEL_W + 2, PANEL_H + 2, FX_B);
}

void podium(uint8_t i, const char *name, int32_t cash, bool active, uint8_t tokens, bool flash) {
    int x = POD_X0 + POD_PITCH * i, y = POD_Y;
    // The podium in play stands in a pulsing outline.
    gfx_fillRect(x - 1, y, POD_PITCH, POD_H, active ? FX_B : INK);
    y++;
    // A rounded front: the header lit along its top, a groove under it, the
    // foot falling into shadow.
    fillRound(x, y, POD_W, POD_H - 2, 2, POD_D[i]);
    gfx_fillRect(x, y, POD_W, 7, POD_C[i]);
    gfx_hline(x + 1, y, POD_W - 2, POD_L[i]);
    gfx_hline(x, y + 7, POD_W, INK);
    dither(x + 1, y + POD_H - 4, POD_W - 2, 2, INK, 0);
    text35(x + POD_W / 2 - text35Width(name) / 2, y + 1, name, i == 1 ? INK : WHITE);
    char buf[14];
    fmtCash(buf, cash);
    int w = gfx_textWidth(buf) + 1;
    uint8_t c = flash ? WHITE : (active ? FX_B : WHITE);
    // Five figures and the comma: the plain weight, a pixel into the edges.
    if (w > POD_W) gfx_text(x + POD_W / 2 - (w - 1) / 2, y + 9, buf, c);
    else raised57(x + POD_W / 2 - w / 2, y + 9, buf, c);
    // Tokens: little tags along the foot.
    if (tokens & 1) gfx_fillRect(x + 2, y + POD_H - 5, 6, 2, FX_A);
    if (tokens & 2) gfx_fillRect(x + POD_W - 8, y + POD_H - 5, 6, 2, CYAN);
}

void promptClear() {
    gfx_fillRect(0, PROMPT_Y, 128, PROMPT_H, INK);
    gfx_hline(0, PROMPT_Y, 128, GOLD);
    gfx_hline(0, PROMPT_Y + 1, 128, WOOD);
}

void promptText(const char *s, uint8_t c) {
    promptClear();
    text35(64 - text35Width(s) / 2, PROMPT_Y + 2, s, c);
}

void promptMenu(const char *const *items, uint8_t n, uint8_t sel) {
    promptClear();
    int gap = n > 3 ? 6 : 10, total = -gap;
    for (uint8_t i = 0; i < n; i++) total += text35Width(items[i]) + gap;
    int x = 64 - total / 2;
    for (uint8_t i = 0; i < n; i++) {
        int w = text35Width(items[i]);
        if (i == sel) {
            // A gold key, its top catching the light.
            fillRound(x - 3, PROMPT_Y + 1, w + 6, 8, 2, GOLD);
            gfx_hline(x - 1, PROMPT_Y + 1, w + 2, WHITE);
            gfx_hline(x - 1, PROMPT_Y + 8, w + 2, WOOD);
            text35(x, PROMPT_Y + 2, items[i], INK);
        } else text35(x, PROMPT_Y + 2, items[i], SILVER);
        x += w + gap;
    }
}

void promptPower(uint8_t power) {
    promptClear();
    text35(3, PROMPT_Y + 2, "HOLD A", WHITE);
    gfx_rect(32, PROMPT_Y + 2, 92, 6, SILVER);
    int w = power * 90 / 255;
    gfx_fillRect(33, PROMPT_Y + 3, w, 4, RED);
    gfx_fillRect(33, PROMPT_Y + 3, w < 60 ? w : 60, 4, GOLD);
    gfx_fillRect(33, PROMPT_Y + 3, w < 30 ? w : 30, 4, FELT_LT);
}

void picker(uint32_t allowed, uint32_t used, uint8_t cur, const char *hint) {
    gfx_fillRect(0, PICK_Y, 128, 128 - PICK_Y, INK);
    gfx_hline(0, PICK_Y, 128, GOLD);
    char s[2] = {0, 0};
    for (uint8_t i = 0; i < 26; i++) {
        int x = 6 + (i % 13) * 9, y = PICK_Y + 3 + (i / 13) * 10;
        s[0] = (char)('A' + i);
        // The letters still to be had are keys; spent ones lie flat.
        if (i == cur && ((allowed >> i) & 1)) {
            fillRound(x - 2, y - 2, 10, 11, 2, FX_B);
            gfx_hline(x - 1, y + 8, 8, WOOD);
            bold57(x, y, s, INK);
        } else if ((allowed >> i) & 1) {
            gfx_fillRect(x - 1, y - 1, 8, 9, NAVY);
            gfx_hline(x - 1, y - 1, 8, BLUE);
            bold57(x, y, s, WHITE);
        } else gfx_text(x, y, s, ((used >> i) & 1) ? NAVY : WINE);
    }
    text35(64 - text35Width(hint) / 2, PICK_Y + 23, hint, SILVER);
}

void summary(const char *title, const char *line, const char *const *names, const int32_t *won,
             const int32_t *total, uint8_t winner, const char *foot) {
    gfx_fillRect(0, STRIP_Y, 128, PROMPT_Y - STRIP_Y, INK);
    edgedRound(3, STRIP_Y + 1, 122, PROMPT_Y - STRIP_Y - 2, 4, FELT_DK, GOLD);
    bold57(64 - (gfx_textWidth(title) + 1) / 2, STRIP_Y + 5, title, FX_B);
    text35(64 - text35Width(line) / 2, STRIP_Y + 15, line, WHITE);
    for (uint8_t i = 0; i < 3; i++) {
        int y = STRIP_Y + 24 + i * 17;
        fillRound(8, y, 112, 15, 3, POD_D[i]);
        fillRound(8, y, 28, 15, 3, POD_C[i]);
        gfx_hline(11, y, 106, POD_L[i]);
        dither(10, y + 13, 108, 2, INK, 0);
        text35(22 - text35Width(names[i]) / 2, y + 5, names[i], i == 1 ? INK : WHITE);
        char buf[14];
        fmtCash(buf, total[i]);
        int w = gfx_textWidth(buf) + 1;
        bold57(116 - w, y + 4, buf, i == winner ? FX_B : WHITE);
        if (won[i]) {                                   // what the round added, if there is room
            *fmtCash(fmtStr(buf, "+"), won[i]) = 0;
            if (40 + text35Width(buf) + 3 < 116 - w) text35(40, y + 5, buf, GOLD);
        }
    }
    promptText(foot, WHITE);
}

}  // namespace board
