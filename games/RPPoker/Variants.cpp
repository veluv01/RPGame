// The table of the four games (Variants.h) and the three tables' stakes and
// names (UNIT: every stake is a multiple of it).
#pragma GCC optimize("Os")
#include "Variants.h"

const Variant VARIANTS[GAMES] = {
    // limit        stud hole str draw 2+3  streets: {deal, up, board, big}
    {NO_LIMIT,      0, 2, 4, 0, 0, {{2, 0, 0, 0}, {0, 0, 3, 0}, {0, 0, 1, 1}, {0, 0, 1, 1}, {}},
     "HOLD'EM", "NO LIMIT - TWO CARDS EACH", "NL HOLD'EM"},
    {FIXED_LIMIT,   0, 5, 2, 1, 0, {{5, 0, 0, 0}, {0, 0, 0, 1}, {}, {}, {}},
     "5 CARD DRAW", "LIMIT - DRAW UP TO THREE", "5 CARD DRAW"},
    {POT_LIMIT,     0, 4, 4, 0, 1, {{4, 0, 0, 0}, {0, 0, 3, 0}, {0, 0, 1, 1}, {0, 0, 1, 1}, {}},
     "OMAHA", "POT LIMIT - USE TWO OF FOUR", "PL OMAHA"},
    {FIXED_LIMIT,   1, 7, 5, 0, 0, {{3, 4, 0, 0}, {1, 1, 0, 0}, {1, 1, 0, 1}, {1, 1, 0, 1}, {1, 0, 0, 1}},
     "7 CARD STUD", "LIMIT - THREE DOWN, FOUR UP", "7 CARD STUD"},
};

const int32_t UNIT[LEVELS] = {1, 5, 25};
const char *const LEVEL_NAME[LEVELS] = {"ROOKIE", "PRO", "SHARK"};
const char *const LEVEL_LINE[LEVELS] = {"CALLS TOO MUCH", "PLAYS THE ODDS", "SMELLS WEAKNESS"};
