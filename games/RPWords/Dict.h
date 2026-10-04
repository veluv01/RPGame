// The dictionary: two lists behind one question.
//
// The core list lives in flash (src/dict/DictData.cpp, built by
// tools/dict/build_dict.py): the common words, so the game plays with no
// card in the slot, and the only list the CPU plays from. The full list
// (ENABLE, ~170,000 words) is the file WORDS.DIC on the SD card
// (tools/dict/build_sd.py); while a card with it is in, your words are
// checked against that instead.
//
// Words here are letters 1..26 (as on the board), not characters.
#pragma once
#include <stdint.h>

namespace dict {

// --- the flash list ---------------------------------------------------------
bool hasCore(const uint8_t *w, uint8_t n);

// Every flash word in turn (the CPU's scan). The words come out a base word
// at a time, each followed by the words made from it.
struct Cursor {
    uint32_t bit;                   // where the next entry starts
    uint32_t mask;                  // the rules that make words from the base word
    uint16_t entry;                 // entries read so far
    uint8_t  len, rule;             // the base word's length; the next rule to try
    uint8_t  base[16];
};
void rewind(Cursor &c);
bool next(Cursor &c, uint8_t *word, uint8_t &n);    // false: no more
uint16_t progress(const Cursor &c);                 // 0..256 through the list

// --- the card ---------------------------------------------------------------
// All three touch the SD card, which shares the LCD's SPI: call them only
// after gfx_wait() (they use the chunk scratch as their sector buffer).
void begin();                       // look for the card and its WORDS.DIC
bool card();                        // the full list is there
uint32_t count();                   // words in the list in use
// The card's list while it answers, else the flash list. A card that stops
// answering is dropped (card() goes false) until begin() finds it again.
bool has(const uint8_t *w, uint8_t n);
// A word off the card, picked by r, as capitals into out (16 bytes). False: no card.
bool randomWord(char *out, uint32_t r);

}  // namespace dict
