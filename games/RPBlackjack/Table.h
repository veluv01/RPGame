// The static-looking parts of the table: back wall, dealer, rail and chip
// rack, felt and its printing, shoe, HUD plaque. The play screen redraws
// them a band at a time, only when something there changed (Presenter).
#pragma once
#include <stdint.h>

class Round;

namespace table {

enum Expr : uint8_t { E_NORMAL, E_ANGRY, E_RAISED, E_BLINK, E_SMILE, E_SURPRISED, E_TALK };

void wall(uint32_t frame);
void dealer(uint8_t expr, uint8_t look, bool alt, int x = 2, int y = 0);
void rail(uint32_t frame);
void felt(const Round &r);
void shoe(uint8_t leftPercent, uint8_t shuffleFrame);
void plaque(int32_t purse, int32_t bet, uint8_t purseFlash);

}  // namespace table
