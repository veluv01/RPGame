// The board's data (Tiles.h): the 40 tiles, the rent table and the
// two card decks.
#pragma GCC optimize("Os")   // cold code: size over speed
#include "Tiles.h"

namespace board {

#define T(type, group, price, aux) { (uint8_t)((type) << 4 | (group)), (uint8_t)((price) / 10), (uint8_t)(aux) }
#define ST(group, price, row) T(STREET, group, price, row)
#define RR T(RAIL, RAILS, 200, 0)

const Tile TILE[TILES] = {
    T(GO, 0, 0, 0),       ST(0, 60, 0),         T(CHEST, 0, 0, 0),    ST(0, 60, 1),         T(TAX, 0, 0, 20),
    RR,                   ST(1, 100, 2),        T(CHANCE, 0, 0, 0),   ST(1, 100, 2),        ST(1, 120, 3),
    T(JAIL, 0, 0, 0),     ST(2, 140, 4),        T(UTIL, UTILS, 150, 0), ST(2, 140, 4),      ST(2, 160, 5),
    RR,                   ST(3, 180, 6),        T(CHEST, 0, 0, 0),    ST(3, 180, 6),        ST(3, 200, 7),
    T(PARKING, 0, 0, 0),  ST(4, 220, 8),        T(CHANCE, 0, 0, 0),   ST(4, 220, 8),        ST(4, 240, 9),
    RR,                   ST(5, 260, 10),       ST(5, 260, 10),       T(UTIL, UTILS, 150, 0), ST(5, 280, 11),
    T(GOTOJAIL, 0, 0, 0), ST(6, 300, 12),       ST(6, 300, 12),       T(CHEST, 0, 0, 0),    ST(6, 320, 13),
    RR,                   T(CHANCE, 0, 0, 0),   ST(7, 350, 14),       T(TAX, 0, 0, 10),     ST(7, 400, 15),
};

const uint16_t RENT[16][6] = {
    {2, 10, 30, 90, 160, 250},        {4, 20, 60, 180, 320, 450},
    {6, 30, 90, 270, 400, 550},       {8, 40, 100, 300, 450, 600},
    {10, 50, 150, 450, 625, 750},     {12, 60, 180, 500, 700, 900},
    {14, 70, 200, 550, 750, 950},     {16, 80, 220, 600, 800, 1000},
    {18, 90, 250, 700, 875, 1050},    {20, 100, 300, 750, 925, 1100},
    {22, 110, 330, 800, 975, 1150},   {24, 120, 360, 850, 1025, 1200},
    {26, 130, 390, 900, 1100, 1275},  {28, 150, 450, 1000, 1200, 1400},
    {35, 175, 500, 1100, 1300, 1500}, {50, 200, 600, 1400, 1700, 2000},
};

uint8_t groupTiles(uint8_t g, uint8_t *tiles) {
    uint8_t n = 0;
    for (uint8_t t = 0; t < TILES; t++)
        if (isDeed(t) && group(t) == g) tiles[n++] = t;
    return n;
}

#define D(amount) (uint8_t)((amount) / 5)

const Card CARD[2][DECK] = {
    {   // Chance
        {MOVE_TO, 39}, {MOVE_TO, 0}, {MOVE_TO, 24}, {MOVE_TO, 11}, {NEAR_RAIL, 0}, {NEAR_RAIL, 0},
        {NEAR_UTIL, 0}, {COLLECT, D(50)}, {GOOJF, 0}, {BACK3, 0}, {GO_JAIL, 0}, {REPAIRS, 0},
        {PAY, D(15)}, {MOVE_TO, 5}, {PAY_EACH, D(50)}, {COLLECT, D(150)},
    },
    {   // Community Chest
        {MOVE_TO, 0}, {COLLECT, D(200)}, {PAY, D(50)}, {COLLECT, D(50)}, {GOOJF, 0}, {GO_JAIL, 0},
        {COLLECT, D(100)}, {COLLECT, D(20)}, {COLLECT_EACH, D(10)}, {COLLECT, D(100)}, {PAY, D(100)},
        {PAY, D(50)}, {COLLECT, D(25)}, {REPAIRS, 1}, {COLLECT, D(10)}, {COLLECT, D(100)},
    },
};

}  // namespace board
