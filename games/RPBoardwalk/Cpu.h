// The CPU players' judgement: what to buy, how high to bid, where to build
// and how to leave jail. Pure functions of the game state.
#pragma once
#include <stdint.h>

namespace cpu {

bool wantsBuy(uint8_t p, uint8_t t);    // at list price
int valuation(uint8_t p, uint8_t t);    // the most p bids for t
int pickBuild(uint8_t p);               // the next house, -1: none
uint8_t jailChoice(uint8_t p);          // 0 roll, 1 pay, 2 play the card

}  // namespace cpu
