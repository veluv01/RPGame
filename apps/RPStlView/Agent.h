// Agent: the "secret agent" look the two apps share. CHSDtoUSB and
// CHStlView carry the same Agent.h/Agent.cpp: change both together.
//
// A 1990s spy's wristwatch LCD, the one a certain N64 game pauses on:
// green on dark glass, and every element a reading rather than decoration.
// It is CHSDtoUSB's instrument panel taken apart: a status bar across the
// top with seven-segment digits, tabs, corner brackets, outlined lettering
// in a bracketed box for the moments that need saying, thin gauges, the
// 3x5 font for labels and the 5x7 for what matters.
//
// The colours are roles, not hues: each app fills them with its own. They
// live in the house palette's slots (rpgame/Palette.h), so the library's
// Sizzle banners come out in them: B_GREEN is PALE, LIVE and MID, outlined
// in BG; B_RED is PALE and ALERT. CHSDtoUSB's are the instrument panel's
// greens on green-black (AGENT_CHROME); CHStlView's are teals taken from
// its wires, on its original blue-black (its PALETTE).
#pragma once
#include <RPGame.h>

namespace agent {

enum : uint8_t {
    BG = INK,           // the glass
    PALE = WHITE,       // the brightest text
    GRID = FELT_DK,     // rules, off segments, empty gauges
    MID = FELT,         // the selected tab, a gauge's fill
    LIVE = FELT_LT,     // live readings
    DIM = SILVER,       // labels
    ALERT = RED,        // trouble
    PANEL = WINE,       // the status bar and the boxes
};
// Slots 1-7 (PALE ... PANEL), RGB444: the instrument panel's greens.
#define AGENT_CHROME 0xBFC, 0x041, 0x2B5, 0x3F6, 0x173, 0xF32, 0x021

// Rows [y0, y1) in one colour, a word at a time (the status bar's ground).
void wipe(int y0, int y1, uint8_t c);

// The 3x5 font (the library's text35): y is the top of the capitals;
// returns the x after the text. tinyR ends at xr.
int tiny(int x, int y, const char *s, uint8_t c);
int tinyW(const char *s);
void tinyR(int xr, int y, const char *s, uint8_t c);
// The 5x7 font (RPGfx's), centred on x = 64.
void centred(int y, const char *s, uint8_t c, uint8_t scale = 1);
// Scale-2 lettering with a 1 px ring, centred.
void outlined(int y, const char *s, uint8_t c, uint8_t ring = BG);

// A seven-segment digit, 5 x 9 (d < 0: all segments off), and a number of
// `digits` of them ending at x (leading zeros off, the last always lit).
void seg7(int x, int y, int d, uint8_t on, uint8_t off = GRID);
void seg7Num(int x, int y, uint32_t v, int digits, uint8_t on, uint8_t off = GRID);

// The status bar: rows 0-11 on PANEL, the word in the 5x7 at x = 13 (an
// icon goes at x = 2), and on the right a three-digit seven-segment
// readout with its unit in two lines of 3x5 ("KB" over "/S").
void statusBar(const char *word, uint8_t c);
void readout(uint32_t v, uint8_t on, const char *unit1, const char *unit2);

// A tag chip, the event log's: w x 7 in c, the tag centred in BG.
void chip(int x, int y, int w, const char *tag, uint8_t c);
// Button hints along row y: "A:OPEN B:UP", each button a MID chip, what it
// does in DIM beside it.
void keys(int y, const char *hints);

// Tabs along row y (3x5): the selected one on MID, the rest DIM, a rule to
// the right edge. Returns the x after the last tab.
int tabs(int y, const char *const *names, int n, int sel);

// Corner brackets around a box, arms `len` long.
void brackets(int x, int y, int w, int h, int len, uint8_t c);

// The alert box: PANEL with a GRID rim and brackets in c, the title
// outlined at scale 2, one or two lines of 5x7 under it.
void alert(int top, const char *title, const char *l1, const char *l2, uint8_t c);

// A thin gauge: w x h, lit up to num/den.
void gauge(int x, int y, int w, int h, uint32_t num, uint32_t den, uint8_t on, uint8_t off = GRID);

// The first n characters of s in the 5x7 (scale 1 or 2), with a block
// cursor after them while `cursor` is set: text typing itself out.
void typed(int x, int y, const char *s, int n, uint8_t c, uint8_t scale = 1, bool cursor = false);

// The intro's sweep: a bright row at y with a tail fading above it.
static const int TAIL = 5;
void sweep(int y);

}  // namespace agent
