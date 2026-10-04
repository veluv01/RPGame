// The screen: a status bar, the card's fill gauge, one graph column per
// READ/WRITE command, and a panel with the event log, the session's
// numbers or the card's details (LEFT/RIGHT). See README "The screen".
#pragma once
#include <stdint.h>

namespace ui {

enum Mode : uint8_t { M_SAFE, M_NOCARD, M_WAITING, M_READY, M_EJECTED };

struct Status {
    uint8_t mode;
    bool ro;
};

void begin();
// The card's identity (CMD10 CID, 16 bytes) and type (Sd2Card::type()),
// or nullptr when there is no card.
void setCard(const uint8_t *cid, uint8_t type);
void button(uint8_t b);                    // a d-pad press (LEFT_BUTTON ...): pages and scrolling
// Draws a frame if one is due (something changed, or an animation runs).
// Returns true if it drew; the caller flushes rows [y0, y1).
bool frame(uint32_t now, const Status &s, int &y0, int &y1);
void goodbye();
struct Perf { uint32_t frames, totalUs, maxUs, rows; };   // drawing time, rows flushed
extern Perf perf;                            // "MENU", before the reset back to the SD menu

}  // namespace ui
