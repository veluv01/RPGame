// The board view's parts: the category strip, the puzzle board's panels,
// the three podiums, the prompt bar, and the letter picker that slides over
// the podiums.
#pragma once
#include <stdint.h>

namespace board {

// How a panel looks. The presenter walks a panel through LIT and the two
// TURN frames to LETTER when its letter is called.
enum Look : uint8_t {
    NONE,       // no panel here (the outer rows' corners)
    EMPTY,      // an unused panel: felt green
    BLANK,      // a letter not yet turned: white
    LIT,        // lit up, about to turn: cyan
    TURN1, TURN2,
    LETTER,     // turned: the character in black
    GUESS,      // solve entry: a typed letter, in blue
};

void strip(const char *category);
void frame();                       // the board's bezel (rows 51..98), panels not drawn
// cursor: the solve entry's panel (a red edge); cursorBox() adds the box
// round it, which lies in the gaps between panels: draw it after the cells.
void cell(uint8_t i, uint8_t look, char ch, bool cursor);
void cursorBox(uint8_t i);

// colour 0..2 = the podium's place. tokens: bit 0 wild card, bit 1 the prize.
void podium(uint8_t i, const char *name, int32_t cash, bool active, uint8_t tokens, bool flash);

void promptClear();
void promptText(const char *s, uint8_t c);
// Up to four choices, the selected one on a gold tab.
void promptMenu(const char *const *items, uint8_t n, uint8_t sel);
void promptPower(uint8_t power);    // "HOLD A" and the bar

// allowed: letters the cursor may rest on. used: called letters (dim).
void picker(uint32_t allowed, uint32_t used, uint8_t cur, const char *hint);

// The end-of-round card over the board and podiums.
void summary(const char *title, const char *line, const char *const *names, const int32_t *won,
             const int32_t *total, uint8_t winner, const char *foot);

}  // namespace board
