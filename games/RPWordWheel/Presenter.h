// Events -> motion: the host's face and speech, panels lighting up and
// turning, the wheel's spin, rolling podium money, banners and confetti.
// CHBlackjack's Presenter pattern: the rules (Show) never draw or play a
// sound; they wait while busy().
#pragma once
#include <stdint.h>

class Show;

namespace present {

void reset(const Show &s);          // a fresh stage: what is shown = the rules' state
void onEvents(Show &s);             // drain the rules' events
void update(const Show &s);         // once per logic tick
bool busy(const Show &s);           // the rules must wait for the show
// The rules asked for a puzzle (Ev::NeedPuzzle): its section, once; else -1.
int8_t takeNeed();
void hurry(bool on);                // A held: speech and CPU spins go faster
// Draw what changed; true if anything was drawn (then overlay() must run).
bool render(const Show &s, uint32_t frame);
void overlay(const Show &s, uint32_t frame);
void invalidate();                  // the next render redraws everything

}  // namespace present
