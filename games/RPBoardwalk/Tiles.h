// The board: 40 tiles clockwise from GO, the rent table and the two card
// decks. Data only - no graphics, so the host tests build it
// (tools/tests/test_rules.cpp).
#pragma once
#include <stdint.h>

namespace board {

enum Type : uint8_t { GO, STREET, RAIL, UTIL, CHANCE, CHEST, TAX, JAIL, PARKING, GOTOJAIL };
// Groups 0..7 are the street colours, brown round to dark blue.
enum : uint8_t { RAILS = 8, UTILS = 9, GROUPS = 10, TILES = 40, JAIL_TILE = 10 };

// aux: a street's row in RENT; a tax's amount / 10.
struct Tile { uint8_t typeGroup, price10, aux; };
extern const Tile TILE[TILES];
// Rent bare, with 1-4 houses, with a hotel.
extern const uint16_t RENT[16][6];

inline uint8_t type(uint8_t t)  { return (uint8_t)(TILE[t].typeGroup >> 4); }
inline uint8_t group(uint8_t t) { return (uint8_t)(TILE[t].typeGroup & 15); }
inline bool isDeed(uint8_t t)   { return type(t) >= STREET && type(t) <= UTIL; }
inline int price(uint8_t t)     { return TILE[t].price10 * 10; }
inline int houseCost(uint8_t t) { return 50 * (1 + group(t) / 2); }
uint8_t groupTiles(uint8_t g, uint8_t *tiles);      // a group's tiles -> how many (2..4)

// Cards. arg: MOVE_TO a tile; an amount / 5; REPAIRS 0 = $25 a house and
// $100 a hotel, 1 = $40 and $115.
enum CardKind : uint8_t {
    MOVE_TO, BACK3, NEAR_RAIL, NEAR_UTIL, COLLECT, PAY, PAY_EACH, COLLECT_EACH, REPAIRS, GO_JAIL, GOOJF,
};
struct Card { uint8_t kind, arg; };
enum : uint8_t { CHANCE_DECK = 0, CHEST_DECK = 1, DECK = 16 };
extern const Card CARD[2][DECK];

}  // namespace board
