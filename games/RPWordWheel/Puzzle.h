// The puzzle board: four rows of panels (12, 14, 14, 12), what is on each,
// which are turned, and the letters called so far. Pure logic.
//
// A puzzle arrives as up to four lines of text, already wrapped by
// tools/phrases/build_bank.py (the device has no word-wrap code). Each line
// is centred on the 14-column grid; one or two lines sit on the middle rows.
// The outer rows only have their 12 middle panels, and the builder never
// puts more than 12 characters on them.
#pragma once
#include <stdint.h>

namespace pz {
constexpr uint8_t ROWS = 4, COLS = 14, CELLS = ROWS * COLS;
constexpr uint8_t TEXT_MAX = 56;                    // 52 characters, 3 breaks, NUL
constexpr uint32_t VOWELS = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 14) | (1u << 20);
constexpr uint32_t ALL = (1u << 26) - 1;
inline bool isLetter(char c) { return c >= 'A' && c <= 'Z'; }
inline uint32_t maskOf(char letter) { return 1u << (letter - 'A'); }
inline bool isVowel(char letter) { return (VOWELS >> (letter - 'A')) & 1; }
// Does the board have a panel at this cell?
inline bool panel(uint8_t i) {
    uint8_t r = i / COLS, c = i % COLS;
    return (r == 1 || r == 2) || (c >= 1 && c <= 12);
}
}  // namespace pz

struct Puzzle {
    char     cell[pz::CELLS];       // 0 = blank panel, else the character on it
    uint8_t  shown[7];              // bit per cell: turned (punctuation starts turned)
    uint32_t used;                  // bit per letter called so far
    uint8_t  nLetters, nHidden;     // letter panels, and those still unturned
    char     category[20];

    void     set(const char *text, const char *cat);
    bool     isShown(uint8_t i) const { return (shown[i >> 3] >> (i & 7)) & 1; }
    void     show(uint8_t i);                       // turn one panel (toss-ups)
    uint8_t  count(char letter) const;              // unturned panels of this letter
    uint8_t  reveal(char letter);                   // marks it used; turns its panels; the count
    void     revealAll();
    uint32_t hiddenMask() const;                    // the letters still to be found
    uint8_t  shownPct() const { return nLetters ? (uint8_t)((nLetters - nHidden) * 100 / nLetters) : 100; }
    // The n-th unturned letter panel (n < nHidden), or 255.
    uint8_t  hiddenCell(uint8_t n) const;
};
