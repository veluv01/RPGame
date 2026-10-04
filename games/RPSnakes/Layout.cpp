// The ladders and snakes (Layout.h). After changing LINK[], paste what
// tools/turns.py prints into Cpu.cpp's TURNS[] (the host tests check it).
#pragma GCC optimize("Os")   // cold code: size over speed
#include "Layout.h"

namespace layout {

const Link LINK[LINKS] = {
    // Ladders, foot to top.
    {3, 24}, {8, 32}, {15, 45}, {21, 59}, {50, 70}, {54, 75}, {63, 84}, {73, 92},
    // Snakes, head to tail. The last one is the big one, in sight of home.
    {26, 5}, {41, 19}, {34, 14}, {64, 38}, {69, 33}, {87, 53}, {99, 61}, {96, 46},
};

int linkAt(uint8_t n) {
    for (uint8_t i = 0; i < LINKS; i++)
        if (LINK[i].from == n) return i;
    return -1;
}

uint8_t landing(uint8_t from, uint8_t steps, uint8_t *via) {
    int n = from + steps;
    if (n > LAST) n = 2 * LAST - n;
    if (via) *via = (uint8_t)n;
    int k = linkAt((uint8_t)n);
    return k < 0 ? (uint8_t)n : LINK[k].to;
}

}  // namespace layout
