// The CPU and the doubling cube, in match play: when to double, and whether
// to take.
//
// The network says how likely the CPU is to win the game (its chance, left
// to the dice and the checkers). The match equity table (src/ai/MetData.cpp,
// from tools/train/met.py) says what winning or losing the game would be
// worth at this score. A double is taken when the chance is at least what
// taking needs to be worth more than passing (the "take point"), a little
// less when the cube would come back live; the CPU doubles when its chance
// is close to the point where the other side should pass, and cashes when
// it is past it - unless a gammon looks likely and is worth playing for.
#pragma once
#include <stdint.h>
#include "Rules.h"

namespace cube {

constexpr int MAX_AWAY = 7;
extern const uint16_t MET[MAX_AWAY][MAX_AWAY];  // Q16: needing a + 1 against b + 1
extern const uint16_t POST[MAX_AWAY];           // Q16: needing b + 1 against 1, after the Crawford game

// The chance (Q16) that a player needing a points wins against one needing
// b. post: the Crawford game has been played.
uint32_t equity(int a, int b, bool post);

// p: the chance (Q16) of winning the game for the side deciding, which
// needs `a` points against `b`; v the cube's value now.
bool wantsTake(int a, int b, int v, bool post, uint32_t p);
// With beavers allowed: a side that should take beavers (and a beavered
// doubler raccoons) when its chance on the other side's roll is this good -
// the double was a mistake, so make it pay twice.
constexpr uint32_t BEAVER_AT = 36045;           // 55%
bool wantsDouble(int a, int b, int v, bool owned, bool post, uint32_t p, bool gammonish);
// The other side looks like losing a gammon: none of its checkers off, and
// three or more still in `side`'s home board or on the bar.
bool gammonish(const bg::Board &b, uint8_t side);

}  // namespace cube
