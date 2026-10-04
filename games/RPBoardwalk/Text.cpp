// The words (Text.h): tile names and the cards' texts.
#pragma GCC optimize("Os")   // cold code: size over speed
#include "Text.h"
#include "Tiles.h"
#include <RPGame.h>

namespace text {

using namespace board;

const char *const DECK_NAME[2] = {"CHANCE", "COMMUNITY CHEST"};

// Tile names in board order (the card tiles take their deck's name). A
// trailing \001..\004 stands for the word most of them end in.
static const char NAMES[] =
    "GO\0" "MEDITERRANEAN\001\0" "\0" "BALTIC\001\0" "INCOME\004\0" "READING\003\0" "ORIENTAL\001\0" "\0"
    "VERMONT\001\0" "CONNECTICUT\001\0"
    "JAIL\0" "ST. CHARLES\002\0" "ELECTRIC COMPANY\0" "STATES\001\0" "VIRGINIA\001\0" "PENNSYLVANIA\003\0"
    "ST. JAMES\002\0" "\0" "TENNESSEE\001\0" "NEW YORK\001\0"
    "FREE PARKING\0" "KENTUCKY\001\0" "\0" "INDIANA\001\0" "ILLINOIS\001\0" "B+O\003\0" "ATLANTIC\001\0"
    "VENTNOR\001\0" "WATER WORKS\0" "MARVIN GARDENS\0"
    "GO TO JAIL\0" "PACIFIC\001\0" "NORTH CAROLINA\001\0" "\0" "PENNSYLVANIA\001\0" "SHORT LINE\0" "\0"
    "PARK\002\0" "LUXURY\004\0" "BOARDWALK";
static const char *const ENDING[4] = {"AVENUE", "PLACE", "RAILROAD", "TAX"};

// The k-th string of a \0-separated list.
static const char *nth(const char *s, uint8_t k) {
    while (k--) { while (*s) s++; s++; }
    return s;
}

char *tile(char *p, uint8_t t, char sep) {
    const char *s = nth(NAMES, t);
    if (!*s) s = DECK_NAME[type(t) == CHEST];
    char *gap = nullptr;
    for (; *s; s++) {
        if (*s < 5) { gap = p; *p++ = ' '; p = fmtStr(p, ENDING[*s - 1]); break; }
        if (*s == ' ') gap = p;
        *p++ = *s;
    }
    *p = 0;
    if (gap) *gap = sep;
    return p;
}

// The cards that only pay or cost money carry a title, in deck order.
static const char TITLES[] =
    // Chance
    "LUCKY STREAK\0" "PARKING FINE\0" "JACKPOT\0"
    // Community Chest
    "BANK SLIP-UP\0" "DOCTOR'S BILL\0" "STOCK SALE\0" "HOLIDAY FUND\0" "TAX REFUND\0" "INSURANCE PAYS\0"
    "HOSPITAL BILL\0" "SCHOOL FEES\0" "CONSULTING FEE\0" "BEAUTY PRIZE\0" "INHERITANCE";

char *card(char *p, uint8_t deck, uint8_t idx) {
    const Card &c = CARD[deck][idx];
    int amount = c.arg * 5;
    switch (c.kind) {
        case MOVE_TO:
            p = tile(fmtStr(p, "ADVANCE TO\n"), c.arg);
            if (!c.arg) p = fmtStr(p, "\nCOLLECT $200");
            break;
        case BACK3:     p = fmtStr(p, "GO BACK\n3 SPACES"); break;
        case NEAR_RAIL: p = fmtStr(p, "ON TO THE NEXT\nRAILROAD\nPAY DOUBLE RENT"); break;
        case NEAR_UTIL: p = fmtStr(p, "ON TO THE NEXT\nUTILITY\nPAY 10X THE DICE"); break;
        case COLLECT: case PAY: {
            // Its title: count the titled cards before it.
            uint8_t k = 0;
            for (uint8_t d = 0; d <= deck; d++)
                for (uint8_t i = 0; i < (d < deck ? DECK : idx); i++)
                    k += CARD[d][i].kind == COLLECT || CARD[d][i].kind == PAY;
            p = fmtStr(p, nth(TITLES, k));
            p = fmtMoney(fmtStr(p, c.kind == PAY ? "\nPAY " : "\nCOLLECT "), amount);
            break;
        }
        case PAY_EACH:     p = fmtMoney(fmtStr(p, "TABLE'S ROUND\nPAY EACH PLAYER\n"), amount); break;
        case COLLECT_EACH: p = fmtStr(fmtMoney(fmtStr(p, "IT'S YOUR DAY\nCOLLECT "), amount), "\nFROM EACH PLAYER"); break;
        case REPAIRS:
            p = fmtStr(p, c.arg ? "STREET REPAIRS\n$40 A HOUSE\n$115 A HOTEL" : "GENERAL REPAIRS\n$25 A HOUSE\n$100 A HOTEL");
            break;
        case GO_JAIL:   p = fmtStr(p, "GO TO JAIL\nDO NOT PASS GO"); break;
        default:        p = fmtStr(p, "GET OUT OF\nJAIL FREE\nKEEP THIS CARD"); break;
    }
    return p;
}

}  // namespace text
