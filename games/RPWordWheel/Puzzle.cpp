#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// The puzzle board (Puzzle.h): the lines laid out on the panels, and the
// panels turned as letters are called.
#include <string.h>
#include "Puzzle.h"

using namespace pz;

void Puzzle::set(const char *text, const char *cat) {
    memset(cell, 0, sizeof cell);
    memset(shown, 0, sizeof shown);
    used = 0;
    nLetters = 0;
    strncpy(category, cat, sizeof category - 1);
    category[sizeof category - 1] = 0;

    uint8_t lines = 1;
    for (const char *p = text; *p; p++) if (*p == '\n') lines++;
    uint8_t row = lines <= 2 ? 1 : 0;
    for (const char *p = text; *p && row < ROWS; row++) {
        uint8_t len = 0;
        while (p[len] && p[len] != '\n') len++;
        uint8_t n = len > COLS ? COLS : len;
        uint8_t col = (uint8_t)((COLS - n) / 2);
        for (uint8_t i = 0; i < n; i++) {
            char ch = p[i];
            if (ch == ' ') continue;
            uint8_t at = (uint8_t)(row * COLS + col + i);
            cell[at] = ch;
            if (isLetter(ch)) nLetters++;
            else shown[at >> 3] |= (uint8_t)(1u << (at & 7));
        }
        p += len;
        if (*p == '\n') p++;
    }
    nHidden = nLetters;
}

void Puzzle::show(uint8_t i) {
    if (i >= CELLS || !isLetter(cell[i]) || isShown(i)) return;
    shown[i >> 3] |= (uint8_t)(1u << (i & 7));
    nHidden--;
}

uint8_t Puzzle::count(char letter) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < CELLS; i++)
        if (cell[i] == letter && !isShown(i)) n++;
    return n;
}

uint8_t Puzzle::reveal(char letter) {
    uint8_t n = 0;
    used |= maskOf(letter);
    for (uint8_t i = 0; i < CELLS; i++)
        if (cell[i] == letter && !isShown(i)) { show(i); n++; }
    return n;
}

void Puzzle::revealAll() {
    for (uint8_t i = 0; i < CELLS; i++) show(i);
}

uint32_t Puzzle::hiddenMask() const {
    uint32_t m = 0;
    for (uint8_t i = 0; i < CELLS; i++)
        if (isLetter(cell[i]) && !isShown(i)) m |= maskOf(cell[i]);
    return m;
}

uint8_t Puzzle::hiddenCell(uint8_t n) const {
    for (uint8_t i = 0; i < CELLS; i++)
        if (isLetter(cell[i]) && !isShown(i) && !n--) return i;
    return 255;
}
