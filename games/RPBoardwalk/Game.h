// A game of Boardwalk: whose turn, the dice, deeds, rent, cards, jail,
// houses, the tap auction, debts, and the events the presentation turns into
// motion. No graphics or sound here, so the host tests can play thousands of
// games (tools/tests/test_rules.cpp).
//
// The flow is CHChess's Match (and CHBlackjack's Round): the game reports
// events and waits while the stage is busy showing them, so the rules never
// outrun the show. The auction alone runs on the clock: a tap takes the
// lead, and the lot is sold when the countdown runs out.
#pragma once
#include <stdint.h>
#include "Tiles.h"

namespace game {

enum : uint8_t { SEATS = 4, BANK = 7, NOBODY = 0xFF };
// Player::kind: OFF, HUMAN, or CPU + its level (0..LEVELS-1).
enum Kind : uint8_t { OFF, HUMAN, CPU };
constexpr uint8_t LEVELS = 3;
// Player::flags: a get-out-of-jail card from each deck; bankrupt.
enum : uint8_t { F_CARD = 1, F_BUST = 4 };

struct Player {
    int32_t cash;
    uint8_t pos;
    uint8_t jail;       // 0: free; else 1 + the rolls missed in there
    uint8_t kind, flags;
};

// Everything a saved game holds.
struct State {
    uint32_t rng;
    Player pl[SEATS];
    uint8_t deed[board::TILES];         // owner (BANK: unowned) | level << 3 (houses; 5: a hotel)
    uint8_t deck[2][board::DECK];       // each deck's order
    uint8_t top[2];
    uint8_t cur, doubles, players;
    uint8_t roundCap;                   // closing time (0: none)
    uint16_t round;                     // from 1
    uint8_t over, winner;               // over: an Over reason + 1
    uint16_t pot;                       // the jackpot on Free Parking: taxes, fines and bail pile up there
};
extern State st;

inline uint8_t owner(uint8_t t) { return (uint8_t)(st.deed[t] & 7); }
inline uint8_t level(uint8_t t) { return (uint8_t)(st.deed[t] >> 3); }
inline bool isCpu(uint8_t p)    { return st.pl[p].kind >= CPU; }
inline bool isHuman(uint8_t p)  { return st.pl[p].kind == HUMAN; }

enum Phase : uint8_t {
    P_OFF,
    P_TURN,         // a turn begins
    P_PREROLL,      // the player at the turn: build, sell, offer a deed; in jail, pay or play a card; roll
    P_MOVE,         // the dice are down: the token goes
    P_RESOLVE,      // it has landed
    P_OFFER,        // on an unowned deed: buy it, or send it to auction
    P_CARD,         // a card is on show
    P_SETTLE,       // payments due (houses and deeds are sold to cover them)
    P_AUCTION,
    P_ENDTURN,
    P_OVER,
};

enum Reason : uint8_t { R_GO, R_RENT, R_TAX, R_CARD, R_JAIL, R_BUY, R_AUCTION, R_BUILD, R_SELL, R_JACKPOT };
enum Act : uint8_t { ACT_ROLL, ACT_PAY, ACT_CARD, ACT_BUY, ACT_AUCTION, ACT_BUILD };
enum Over : uint8_t { BY_BANKRUPTCY, BY_CLOSING };

enum Ev : uint8_t {
    EV_START,       // a game began (a = 1: the deal follows) or was restored: redraw from the state
    EV_DEAL,        // a = player, b = tile: a deed dealt at the start
    EV_TURN,        // a = player, b = 1 if a human, c = doubles thrown so far this turn
    EV_PICK,        // the CPU chose: a = Act, b = tile (the glove presses the button a player would)
    EV_DICE,        // a, b = the dice, c = 1 if doubles
    EV_MOVE,        // a = player, b = from, c = to, amount = tiles (negative: backwards)
    EV_LAND,        // a = player, b = tile, amount = the jackpot waiting there (Free Parking)
    EV_OFFER,       // a = player, b = tile, amount = its price: buy or auction?
    EV_BUY,         // a = player, b = tile
    EV_AUCTION,     // a = tile, b = seller (BANK or a player)
    EV_BID,         // a = player, amount = the price now
    EV_SOLD,        // a = winner, b = tile, c = seller, amount = price
    EV_PAY,         // a = from, b = to (BANK or a player), c = Reason, amount
    EV_CARD,        // a = deck, b = card, c = player
    EV_JAIL,        // a = player, b = the tile it was sent from
    EV_JAILOUT,     // a = player, b = how: 0 doubles, 1 paid, 2 a card
    EV_BUILD,       // a = tile, b = its level now, c = player
    EV_SELL,        // the same, a house sold back
    EV_BANKRUPT,    // a = player
    EV_LASTROUND,
    EV_OVER,        // a = winner, b = Over
};
struct Event {
    uint8_t type, a, b, c;
    int16_t amount;
};

struct Setup {
    uint8_t kind[SEATS];    // Kind per seat; the seats in use close up
    uint8_t roundCap;
    uint32_t seed;
    uint8_t deal;           // deeds dealt to each player (0: the usual number)
};

void start(const Setup &s);
void update(bool stageBusy);        // once per logic tick
Phase phase();
inline bool active() { return phase() != P_OFF && phase() != P_OVER; }
bool humanToAct();                  // waiting for the player at the turn (a human)

bool ownsGroup(uint8_t p, uint8_t g);
bool majority(uint8_t p, uint8_t g);    // p holds most of the group: it can build there
int rent(uint8_t t, uint8_t roll);  // what landing on an owned deed costs (a utility: with this roll)
int netWorth(uint8_t p);            // cash + deeds at list price + buildings at cost
uint8_t lastRoll();

// The player at the turn, before rolling.
bool roll();
bool payJail();
bool useJailCard();
bool canBuild(uint8_t t);
bool build(uint8_t t);
bool canSell(uint8_t t);
bool sell(uint8_t t);
bool canOffer(uint8_t t);
bool offer(uint8_t t);              // put a deed up for auction
// ... and on an unowned deed.
bool buy();
bool decline();                     // to auction

// The auction. The bank's own lot starts at nothing: the first bid leads
// at $0 (unbid, it goes free to the next seat). A player's lot opens with
// the bank's bid of half its price. Each bid raises the price a step and
// restarts the countdown; when it runs out the lot goes to the leader.
enum : uint8_t { AUCTION_OPEN = 210, AUCTION_WINDOW = 150, AUCTION_LAST = 50 };
struct Auction {
    uint8_t tile, seller, leader;   // leader: a player, BANK, or NOBODY yet
    uint8_t started;                // the clock is running (it waits for the stage to open the lot)
    uint8_t timer, step;
    int16_t price;
    int16_t limit[SEATS];           // CPUs: the most each will pay
    uint8_t wait[SEATS];            // ... and ticks until it may bid
};
extern Auction auc;
bool canBid(uint8_t p);
bool bid(uint8_t p);                // seat p taps

bool popEvent(Event &e);
bool peekEvent(Event &e);

// Saved games hold the state as the turn began: continuing replays the turn.
const State &checkpoint();
bool restore(const State &s);

// Debug and tests: the next rolls (d1, d2 queued, up to 8 dice).
void forceDice(uint8_t d1, uint8_t d2);

}  // namespace game
