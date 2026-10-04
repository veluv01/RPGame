// Drawing the two machines: the cabinet's top (marquee or jackpot meters),
// the reel window and the strip under it with the bar. What to draw comes
// from the rules (Slots) and from the presenter's View, which holds
// everything that is animation rather than fact.
#pragma once
#include <stdint.h>
#include "Slots.h"

namespace mach {

// The felt (FELT_DK, FELT, FELT_LT), one per machine, in Machine's order
// (Slots.h): the casino's green, DRAGON FORTUNE's maroon, jade and
// orange, or SWEET's pink, mint and lilac (each machine's symbols are drawn
// for its own). The sketch gives them to pal::setThemes() once.
enum Theme : uint8_t { GREEN, FORTUNE, SWEET, THEME_COUNT };
extern const uint16_t THEMES[THEME_COUNT][3];

constexpr int8_t LINE_NONE = -1, LINE_ALL = 25;

enum ReelState : uint8_t { STOPPED, SPINNING, LANDING };

struct View {
    int32_t pos[5];             // reel position: strip stops in Q8, top row
    uint8_t state[5];           // ReelState
    uint8_t stop[5];            // where each reel landed (or will)
    uint8_t cell[5][ROWS];      // the symbols there
    uint8_t coin[5][ROWS];      // K_* shown on coin cells
    uint8_t wildH[5];           // rows of each reel the dragon has filled so far, from the bottom (0..72)
    int8_t  line;               // line lit: LINE_NONE, 0..24 or LINE_ALL
    uint8_t antic;              // reel being waited on + 1 (0 = none)
    uint8_t arm;                // LUCKY 7's arm: 0 up .. 64 pulled
    uint8_t rush;               // SWEET: the ladder step lit (it moves once the reels have shown why)
    bool    holdMode;           // Hold and Spin: 15 cells instead of reels
    uint16_t cellSpin;          // cells still spinning (hold mode)
    uint16_t cellHide;          // new coins not landed yet (hold mode)
    uint8_t flashCell;          // cell being counted + 1 (hold mode)
    int32_t shownPurse, shownWin;
    int32_t meter[2];           // MAJOR and GRAND as shown (they tick up to the real ones)
    bool    wheelOn, wheelLit;  // LUCKY 7's bonus wheel over the reels; its winning segment flashing
    uint16_t wheelAngle;        // 1/65536 turn
    const char *msg;            // the line under the reels
    bool    canSpin, pressed;   // the SPIN button
};

int reelX(uint8_t machine, uint8_t reel);       // left edge of a reel's cells
int winY(uint8_t machine);
// A symbol with its top-left at (x, y); k = what a coin carries (K_NONE: plain).
void symbol(const Slots &g, uint8_t sym, int x, int y, uint8_t k = K_NONE);

void top(const Slots &g, const View &v, uint32_t frame);
// full = false: only the reels (and the wheel) moved since the last draw.
void window(const Slots &g, const View &v, uint32_t frame, bool full);
void low(const Slots &g, const View &v, uint32_t frame);
// The paytable, a list scrolled to `scroll` pixels (0..payScrollMax).
void paytable(const Slots &g, int scroll);
int payScrollMax(const Slots &g);
constexpr int PAY_PAGE = 78;                    // three rows: what A and B skip

}  // namespace mach
