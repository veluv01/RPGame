// The show. The rules settle a spin in one call; this replays it: reels
// turning and thumping home one by one (the last ones held back when a
// feature is a symbol away), the dragon filling its reel, the lines lighting,
// the win counting up into the purse, coins locking in Hold and Spin, and
// the banners, fountains and shakes that say how big it was.
//
// It also draws the play screen, in three bands (top, reels, bottom), each
// redrawn only when what it shows changed or something moved across it.
#pragma once
#include <stdint.h>
#include "Slots.h"

namespace present {

void reset(const Slots &g);         // sitting down at a machine
void welcome(const Slots &g);       // ... and its name in lights
void invalidate();                  // redraw everything next frame

void spin(const Slots &g);          // g.spin() was just called
void respin(const Slots &g);        // g.respin() was just called
void hurry();                       // a button during the show: stop the reels, finish the count
bool busy();                        // still showing: the rules wait
bool spinning();                    // reels still turning

void setArm(uint8_t pull, bool held);   // LUCKY 7's arm, 0..64: held it follows the pull; let go, it
                                        // swings down to the stop and bounces back (0 = put it at rest)
void setButton(bool canSpin, bool pressed);
void setFast(bool fast);

void update(const Slots &g);        // once per logic tick
void render(const Slots &g, uint32_t frame);

}  // namespace present
