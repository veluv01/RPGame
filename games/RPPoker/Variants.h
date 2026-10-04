// The four games as data: what is dealt on each street, how much may be bet,
// and the stakes of the three tables.
// (Variants, not Variant: on a case-insensitive disk a Variant.h in the
// sketch folder is found in place of the board core's variant.h.)
#pragma once
#include <stdint.h>

enum Game : uint8_t { HOLDEM, DRAW, OMAHA, STUD, GAMES };
enum Limit : uint8_t { NO_LIMIT, POT_LIMIT, FIXED_LIMIT };

// One betting round's cards: `deal` to every live seat (bit k of `up` set:
// the k-th of them face up), then `board` community cards. `big`: fixed
// limit bets are the big bet from this street on.
struct Street { uint8_t deal, up, board, big; };

struct Variant {
    uint8_t limit;
    uint8_t stud;           // antes and a bring-in instead of blinds; face-up cards
    uint8_t hole;           // cards each player ends with (not counting the board)
    uint8_t streets;        // betting rounds
    uint8_t draw;           // Five Card Draw: the draw comes before street 1
    uint8_t exactTwo;       // Omaha: two hole cards and three from the board
    Street  st[5];
    const char *name, *line, *hud;
};

extern const Variant VARIANTS[GAMES];

// The tables. Every stake is a multiple of the table's unit:
//   blinds u / 2u; fixed-limit bets 2u / 4u; stud ante u, bring-in u;
//   buy-in 40u..200u (20 to 100 big blinds).
constexpr uint8_t LEVELS = 3;
enum : uint8_t { ROOKIE, PRO, SHARK };
extern const int32_t UNIT[LEVELS];
extern const char *const LEVEL_NAME[LEVELS];
extern const char *const LEVEL_LINE[LEVELS];
inline int32_t minBuyIn(uint8_t level) { return 40 * UNIT[level]; }
inline int32_t maxBuyIn(uint8_t level) { return 200 * UNIT[level]; }
