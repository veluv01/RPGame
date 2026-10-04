// The play screen's presentation (Stage.h): turns the game's events into
// motion - tokens, dice, cards, coins, the auction, the camera - and
// draws it.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library)
#include <string.h>
#include <RPGame.h>
#include <Arduino.h>
#include "config.h"
#include "Stage.h"
#include "Game.h"
#include "Text.h"
#include "Iso.h"
#include "Cards.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace stage {

using namespace iso;
using namespace board;
using namespace game;

// Cherries, banana, apple, strawberry.
const uint8_t SEAT_COLOUR[4] = {RED, GOLD, FELT_LT, SKIN};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool fast;

// The game as shown: it trails the rules by whatever is still in the air.
static uint8_t sDeed[TILES];
static uint8_t sPos[SEATS];                 // NOBODY: its token is in the air
static int32_t sCash[SEATS], cashTo[SEATS]; // the HUD's counters roll towards their targets
static uint8_t hudFlash[SEATS];

static uint8_t turn;
static bool humanTurn, glove;
static uint8_t focus;                       // the tile the camera and the plate attend to
static uint8_t tapT;                        // the glove's tap
static uint8_t holdT;                       // frames the stage keeps the game waiting
static bool waitPress;                      // ... or until a button
static bool barOn;
static int8_t manageTile = -1;
static uint8_t pickAct, pickT;              // the CPU's choice, lit on the bar
static uint8_t lastHuman = NOBODY;          // for the hand-over between players
static bool over, overDone;
static uint8_t overT, overWhy, winner;

// A token on the move: hopping tile to tile, or one long jump (to jail).
struct Mover { uint8_t seat, at, to, t, on, jump; int8_t dir; };
static Mover mv;

// The dice, in screen space: tumbling in for diceFrames(), then at rest.
static uint8_t diceA, diceB, diceT;
static bool diceDouble;

// The card on show: a deed (on the left) or a Chance / Chest card.
enum : uint8_t { POP_NONE, POP_DEED, POP_CARD };
static const uint8_t FLIP = 8;
static uint8_t popKind, popA, popB, popT;
static bool popClosing, closeAfterHold;
static bool aucOn;
static uint8_t bidPop;

static uint8_t growTile = NOBODY, growT;    // a house just built, popping up
static uint8_t topSeat = NOBODY, topT;      // bankrupt: its token topples
static uint8_t setTile = NOBODY;            // a deed just changed hands: did it make a set?
static uint8_t setGroup, setT;              // ... it did: the group lights up

// Coins in flight (screen space); the last of a payment carries the amount.
struct Fly { int16_t x0, y0, x1, y1, amount; int8_t t; uint8_t seat, on; };
static Fly flies[6];
static const int8_t FLY_T = 16;

// The plate's words: two parts, each its own colour.
static char plText[40];
static uint8_t plSplit, plC[2], plT;

// Camera: eased (world, Q4) towards its aim.
static int32_t cx16, cy16;
static int16_t aimX, aimY;
static uint8_t aimShift = 2;
// The whip zoom: iso::tileH steps towards zoomTo, one step each frame drawn.
static uint8_t zoomTo = 5;
static bool zoomDrawn = true;
// The glove's tip, world, Q4.
static int32_t gx16, gy16;

// A CPU's turn goes by at the QUICK pace: the show is for your own.
static bool quick() { return fast || !humanTurn; }
static uint8_t hopFrames() { return mv.jump ? 30 : quick() ? 4 : 7; }
static uint8_t diceFrames() { return quick() ? 14 : 30; }

static const uint8_t RM_CPU[16] = {0, 1, 2, 3, 4, 5, 6, 7, RED, WINE, 10, 11, 12, 13, 14, 15};   // a CPU's red-cuffed glove

// ---------------------------------------------------------------------------
// Geometry and the camera
// ---------------------------------------------------------------------------

// Where a seat's token stands on a tile: a slot each, two abreast and two
// deep, so nothing shuffles about when a tile fills up.
static void slot(uint8_t t, uint8_t seat, int &x, int &y) {
    if (corner(t)) place(t, seat & 1 ? 11 : 5, seat & 2 ? 12 : 6, x, y);
    else place(t, seat & 1 ? 6 : 2, seat & 2 ? 14 : 9, x, y);
}

static bool cardLeft() { return popKind == POP_DEED || manageTile >= 0; }

// Frame a world point, never past the board's edges (no empty carpet for
// nothing). The view is 128 x 118 below the HUD. With a deed card up on the
// left, the point is framed in what is left of the screen beside it.
static void aimAt(int x, int y, uint8_t shift) {
    int x0 = -N * hw() - 6, x1 = N * hw() + 6;
    int y0 = -zoomed(16), y1 = 2 * N * hh() + slab() + 10;
    if (cardLeft()) { x -= 30; x0 -= 60; }
    if (manageTile >= 0) { y -= 24; y0 -= 40; }             // ... and below the keys on the right
    if (x < x0 + 64) x = x0 + 64;
    if (x > x1 - 64) x = x1 - 64;
    if (y < y0 + 59) y = y0 + 59;
    if (y > y1 - 59) y = y1 - 59;
    aimX = (int16_t)x; aimY = (int16_t)y; aimShift = shift;
}

// A tile; at rest, with the stretch of board ahead of it in view (where
// the dice might take you).
static void aimTile(uint8_t t, uint8_t shift) {
    int x, y, ax, ay;
    worldOf(t, x, y);
    if (zoomTo == 5 && !cardLeft() && !aucOn && !over) {
        worldOf((uint8_t)((t + 4) % TILES), ax, ay);
        x = (x + ax) / 2; y = (y + ay) / 2;
    }
    aimAt(x, y - zoomed(4), shift);
}

static void snapCamera() {
    cx16 = aimX << 4; cy16 = aimY << 4;
    cam.x = aimX; cam.y = aimY;
}

static void stepCamera() {
    int32_t dx = (aimX << 4) - cx16, dy = (aimY << 4) - cy16;
    cx16 += dx >> aimShift; cy16 += dy >> aimShift;
    if (dx > -16 && dx < 16) cx16 = aimX << 4;
    if (dy > -16 && dy < 16) cy16 = aimY << 4;
    cam.x = (int)(cx16 >> 4);
    cam.y = (int)(cy16 >> 4);
}

// Everything held in world space keeps its place on the board.
void setZoom(uint8_t h) {
    uint8_t o = tileH;
    if (h == o) return;
    tileH = h;
    cx16 = cx16 * h / o; cy16 = cy16 * h / o;
    aimX = (int16_t)(aimX * h / o); aimY = (int16_t)(aimY * h / o);
    gx16 = gx16 * h / o; gy16 = gy16 * h / o;
}

static void moverPos(int &x, int &y, int &z) {
    int ax, ay, bx, by, T = hopFrames();
    slot(mv.at, mv.seat, ax, ay);
    slot(mv.jump ? mv.to : (uint8_t)((mv.at + mv.dir + TILES) % TILES), mv.seat, bx, by);
    int e = fx::ease(mv.jump ? fx::IN_OUT : fx::LINEAR, mv.t, T);
    x = ax + (((bx - ax) * e) >> 8);
    y = ay + (((by - ay) * e) >> 8);
    z = (zoomed(mv.jump ? 30 : 5) * fx::isin(mv.t * 128 / T)) >> 8;
}

// ---------------------------------------------------------------------------
// Public controls
// ---------------------------------------------------------------------------
void setFast(bool on) { fast = on; }
void lookAt(uint8_t t) { focus = t; aimTile(t, 2); snapCamera(); }
void setBar(bool on) { barOn = on; }
uint8_t picked() { return pickT ? pickAct : 0; }
bool waiting() { return waitPress; }
bool overShown() { return overDone; }

void acknowledge() {
    waitPress = false;
    fx::holdBanner(false);
}

void manage(int t) {
    manageTile = (int8_t)t;
    if (t >= 0) focus = (uint8_t)t;
    else focus = st.pl[turn].pos;
}

bool busy() {
    return mv.on || holdT || waitPress || (diceT && diceT <= diceFrames()) || (popKind && (popT < FLIP || popClosing)) ||
           tileH != zoomTo || (topT && topT < 40);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
static void say(const char *a, uint8_t ca, const char *b, uint8_t cb) {
    char *p = fmtStr(plText, a);
    plSplit = (uint8_t)(p - plText);
    fmtStr(p, b);
    plC[0] = ca; plC[1] = cb;
    plT = 1;
}

static char *seatTag(char *b, uint8_t p) {
    b[0] = 'P'; b[1] = (char)('1' + p); b[2] = 0;
    return b + 2;
}

// A party's spot on screen: its HUD slot, or for the bank the middle of the
// view (where the token or the card is).
static void spot(uint8_t p, int &x, int &y) {
    if (p == BANK) { x = CX; y = CY - 6; }
    else { x = p * 32 + 16; y = 6; }
}

static void fly(int x0, int y0, int x1, int y1, int delay, uint8_t seat, int amount) {
    for (auto &f : flies) {
        if (f.on) continue;
        f.x0 = (int16_t)x0; f.y0 = (int16_t)y0; f.x1 = (int16_t)x1; f.y1 = (int16_t)y1;
        f.amount = (int16_t)amount; f.t = (int8_t)-delay; f.seat = seat; f.on = 1;
        return;
    }
    if (seat != NOBODY) cashTo[seat] += amount;             // no room in the air: paid at once
}

static void sync(bool fresh) {
    for (uint8_t p = 0; p < SEATS; p++) {
        sPos[p] = st.pl[p].pos;
        sCash[p] = cashTo[p] = st.pl[p].cash;
    }
    memcpy(sDeed, st.deed, sizeof sDeed);
    if (fresh) memset(sDeed, BANK, sizeof sDeed);           // the deal is still to come
    memset(flies, 0, sizeof flies);
    memset(&mv, 0, sizeof mv);
    fx::clear();
    diceT = popKind = holdT = pickT = plT = topT = overT = 0;
    waitPress = popClosing = closeAfterHold = aucOn = over = overDone = glove = false;
    topSeat = growTile = lastHuman = setTile = NOBODY;
    setT = 0;
    manageTile = -1;
    turn = st.cur;
    setZoom(zoomTo = 5);
    lookAt(st.pl[st.cur].pos);
}

void reset() {
    sync(false);
    humanTurn = isHuman(turn);
    glove = phase() == P_PREROLL;
}

static void closePopup() { if (popKind) popClosing = true; }

static void openPopup(uint8_t kind, uint8_t a, uint8_t b) {
    if (popKind == kind && popA == a && !popClosing) return;        // already up
    popKind = kind; popA = a; popB = b; popT = 0;
    popClosing = closeAfterHold = false;
    diceT = 0;                                   // the dice have had their say
    audio::sfx(Sfx::Flip);
}

static uint8_t humans() {
    uint8_t n = 0;
    for (uint8_t p = 0; p < st.players; p++) n += isHuman(p);
    return n;
}

static void onTurn(const Event &e) {
    turn = e.a;
    humanTurn = e.b != 0;
    glove = true;
    diceT = 0;
    zoomTo = 5;
    focus = sPos[turn] == NOBODY ? st.pl[turn].pos : sPos[turn];
    closePopup();
    aucOn = false;
    if (!e.c) plT = 0;
    if (!e.c && !turn && st.players == SEATS) {             // four at the table: no room for the round on the HUD
        // (the bar hides the plate on your turn: the round floats up instead)
        char buf[12], *p = fmtInt(fmtStr(buf, "R"), st.round);
        if (st.roundCap) fmtInt(fmtStr(p, "/"), st.roundCap);
        fx::floatText(buf, CX, 30, st.round == st.roundCap ? RED : GOLD);
    }
    if (!humanTurn) { holdT = 10; return; }
    // Several humans: the handheld changes hands.
    if (humans() > 1 && lastHuman != turn) {
        char name[10], *p = fmtStr(name, "PLAYER ");
        *p++ = (char)('1' + turn);
        *p = 0;
        fx::banner(name, fx::B_GOLD, 40, 50);
        holdT = 30;
    }
    lastHuman = turn;
    audio::sfx(Sfx::Turn);
    audio::led(audio::LED_BLINK);
}

static void onPay(const Event &e) {
    int x0, y0, x1, y1, amount = e.amount;
    char buf[12];
    spot(e.a, x0, y0);
    spot(e.b, x1, y1);
    if (e.a != BANK) {
        cashTo[e.a] -= amount;
        hudFlash[e.a] = 12;
        fmtMoney(buf, -amount);
        fx::floatText(buf, x0, 34, RED);
    }
    if (e.b != BANK) {
        fmtMoney(fmtStr(buf, "+"), amount);
        fx::floatText(buf, x1, e.a == BANK ? 40 : 34, GOLD);
    }
    // A coin per $50 or so, the last one bringing the money.
    int n = amount / 50 + 1;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++)
        fly(x0, y0, x1, y1, i * 4, i == n - 1 && e.b != BANK ? e.b : (uint8_t)NOBODY, amount);
    audio::sfx(e.a == BANK ? Sfx::Coin : Sfx::Pay);
    if (e.c == R_JACKPOT) {
        fx::banner("JACKPOT!", fx::B_RAINBOW, 40, 90);
        fx::fountain(fx::COIN, CX, 90, 16);
        audio::sfx(Sfx::Win);
        audio::led(audio::LED_PARTY);
        holdT = 60;
    }
    if (e.c == R_GO && amount > 200) {
        fx::banner("PAYDAY!", fx::B_RAINBOW, 40, 70);
        fx::fountain(fx::COIN, CX, 90, 12);
        audio::sfx(Sfx::Win);
        holdT = quick() ? 30 : 50;
    }
    if (e.c == R_RENT || e.c == R_TAX) {
        fmtMoney(fmtStr(buf, e.c == R_RENT ? "RENT " : "TAX "), amount);
        // Red when you pay it, gold when a CPU pays you.
        fx::banner(buf, e.b < SEATS && isHuman(e.b) && !isHuman(e.a) ? fx::B_GOLD : fx::B_RED, 40, 50);
        holdT = quick() ? 20 : 40;
        if (amount >= 300) {                                // a hotel's worth: it lands like one
            fx::shake(14, 3);
            fx::fountain(fx::COIN, CX, 90, 10);
            audio::led(audio::LED_TRIPLE);
            holdT = (uint8_t)(holdT + 20);
        }
    }
}

static void onLand(uint8_t t, int pot) {
    char name[28], tag[12];
    int x, y;
    focus = t;
    worldOf(t, x, y);
    if (!quick()) zoomTo = 10;
    fx::burst(fx::DUST, toScreenX(x), toScreenY(y), 10, zoomed(24), t & 1 ? darkTile : lightTile);
    fx::shake(4, 1);
    audio::sfx(Sfx::Land);
    // The plate: the tile, and its price or its owner.
    text::tile(name, t);
    tag[0] = 0;
    uint8_t c = GOLD;
    if (isDeed(t)) {
        uint8_t o = sDeed[t] & 7;
        if (o == BANK) fmtMoney(fmtStr(tag, " "), price(t));
        else {
            // Whose, and what it charges.
            tag[0] = ' ';
            fmtMoney(fmtStr(seatTag(tag + 1, o), " "), rent(t, lastRoll()));
            c = SEAT_COLOUR[o];
        }
    }
    if (pot) fmtMoney(fmtStr(tag, " "), pot);
    say(name, WHITE, tag, c);
    holdT = quick() ? 4 : 14;
}

// A deed that gives its new owner most of its colour (so they may build
// there) or all of it: the group lights up and the table hears about it.
static void setCall() {
    uint8_t t = setTile, o = (uint8_t)(sDeed[t] & 7), tiles[4], have = 0;
    setTile = NOBODY;
    if (type(t) != STREET || o >= SEATS) return;
    uint8_t n = groupTiles(group(t), tiles);
    for (uint8_t i = 0; i < n; i++) have += (sDeed[tiles[i]] & 7) == o;
    if (have == n) fx::banner("FULL SET!", fx::B_RAINBOW, 40, 70);
    else if (2 * have > n && 2 * (have - 1) <= n) fx::banner("CAN BUILD!", fx::B_GOLD, 40, 60);
    else return;
    setGroup = group(t);
    setT = 60;
    focus = t;
    fx::fountain(fx::CONFETTI, CX, 70, 14);
    audio::sfx(Sfx::Doubles);
    audio::led(audio::LED_TRIPLE);
    holdT = quick() ? 30 : 56;
}

// A seat's colour; the bank's is silver.
static uint8_t colourOf(uint8_t p) { return p < SEATS ? SEAT_COLOUR[p] : SILVER; }

// The plate: a deed and whose it is now.
static void owned(uint8_t p, uint8_t t) {
    char name[28], tag[4];
    text::tile(name, t);
    seatTag(tag, p)[0] = ' ';
    tag[3] = 0;
    say(p < SEATS ? tag : "BANK ", colourOf(p), name, WHITE);
}

static void onEvent(const Event &e) {
    int x, y;
    switch (e.type) {
        case EV_START: sync(e.a != 0); break;
        case EV_DEAL:
            sDeed[e.b] = e.a;
            owned(e.a, e.b);
            spot(e.a, x, y);
            fly(CX, CY, x, y, 0, NOBODY, 0);
            audio::sfx(Sfx::Flip);
            hudFlash[e.a] = 10;
            holdT = fast ? 8 : 16;
            break;
        case EV_TURN: onTurn(e); break;
        case EV_PICK:
            pickAct = (uint8_t)(e.a + 1);
            pickT = fast ? 10 : 18;
            holdT = pickT;
            tapT = 1;
            if (e.a == ACT_BUILD) focus = e.b;
            audio::sfx(Sfx::Select);
            break;
        case EV_DICE:
            diceA = e.a; diceB = e.b; diceDouble = e.c != 0;
            diceT = 1;
            glove = false;
            zoomTo = 5;
            closePopup();
            audio::sfx(Sfx::Dice);
            break;
        case EV_MOVE:
            mv.seat = e.a; mv.at = e.b; mv.to = e.c; mv.t = 0; mv.on = 1; mv.jump = 0;
            mv.dir = (int8_t)(e.amount < 0 ? -1 : 1);
            sPos[e.a] = NOBODY;
            zoomTo = 5;
            plT = 0;
            closePopup();
            break;
        case EV_LAND: onLand(e.b, e.amount); break;
        case EV_OFFER: openPopup(POP_DEED, e.b, 0); break;
        case EV_BUY:
            sDeed[e.b] = e.a;
            owned(e.a, e.b);
            fx::burst(fx::STAR, 34, 50, 10, 30, SEAT_COLOUR[e.a]);
            audio::sfx(Sfx::Buy);
            holdT = quick() ? 14 : 34;
            closeAfterHold = true;
            setTile = e.b;
            break;
        case EV_AUCTION:
            focus = e.a;
            openPopup(POP_DEED, e.a, 0);
            aucOn = true;
            holdT = fast ? 14 : 30;                  // a moment to see the lot before the clock starts
            break;
        case EV_BID:
            bidPop = 6;
            hudFlash[e.a] = 8;
            audio::blip((uint16_t)(1400 + (e.amount > 1500 ? 1500 : e.amount)), 40);
            break;
        case EV_SOLD:
            sDeed[e.b] = e.a;
            owned(e.a, e.b);
            // Under half its price to a player: a steal.
            fx::banner(!e.amount ? "FREE!" : e.a < SEATS && 2 * e.amount < price(e.b) ? "A STEAL!" : "SOLD!",
                       e.amount ? fx::B_GOLD : fx::B_CYAN, 60, 60);
            fx::burst(fx::STAR, 96, 34, 12, 30, colourOf(e.a));
            fx::shake(6, 2);
            audio::sfx(Sfx::Gavel);
            holdT = fast ? 30 : 56;
            closeAfterHold = true;
            setTile = e.b;
            break;
        case EV_PAY: onPay(e); break;
        case EV_CARD:
            openPopup(POP_CARD, e.a, e.b);
            if (humanTurn) waitPress = true;
            else holdT = fast ? 40 : 70;
            break;
        case EV_JAIL:
            fx::banner("GO TO JAIL", fx::B_RED, 40, 60);
            fx::shake(8, 2);
            audio::sfx(Sfx::Jail);
            mv.seat = e.a; mv.at = e.b; mv.to = JAIL_TILE; mv.t = 0; mv.on = 1; mv.jump = 1; mv.dir = 1;
            sPos[e.a] = NOBODY;
            zoomTo = 5;
            closePopup();
            break;
        case EV_JAILOUT:
            fx::floatText(e.b == 2 ? "CARD!" : "OUT!", CX, CY - 24, CYAN);
            break;
        case EV_BUILD: case EV_SELL:
            sDeed[e.a] = (uint8_t)(e.c | e.b << 3);
            focus = e.a;
            worldOf(e.a, x, y);
            fx::burst(fx::DUST, toScreenX(x), toScreenY(y), 8, zoomed(20), WHITE);
            holdT = quick() ? 8 : 16;
            if (e.type == EV_BUILD) {
                growTile = e.a; growT = 1;
                audio::sfx(Sfx::Build);
                if (e.b == 5) {
                    fx::banner("HOTEL!", fx::B_GOLD, manageTile >= 0 ? 84 : 40, 50);     // (clear of the manage view's keys)
                    fx::burst(fx::STAR, toScreenX(x), toScreenY(y) - 10, 12, 40, GOLD);
                    holdT = quick() ? 20 : 40;
                }
            }
            break;
        case EV_BANKRUPT:
            fx::banner("BANKRUPT!", fx::B_RED, 40, 110);
            fx::shake(10, 2);
            audio::sfx(Sfx::Lose);
            topSeat = e.a; topT = 1;
            plT = diceT = 0;
            focus = sPos[e.a] == NOBODY ? st.pl[e.a].pos : sPos[e.a];
            zoomTo = fast ? 5 : 10;
            holdT = 110;
            break;
        case EV_LASTROUND:
            fx::banner("LAST ROUND", fx::B_CYAN, 40, 70);
            audio::sfx(Sfx::Doubles);
            holdT = 50;
            break;
        case EV_OVER:
            over = true;
            overT = 0;
            winner = e.a; overWhy = e.b;
            glove = false;
            closePopup();
            aucOn = false;
            break;
    }
}

void begin() {}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------
void update() {
    Event e;
    // A new game cuts in on anything still showing, and the auction's bids
    // come as they are made; everything else waits its turn.
    while (peekEvent(e) && (!busy() || e.type == EV_START || e.type == EV_BID)) {
        popEvent(e);
        onEvent(e);
    }

    if (holdT && !--holdT) {
        if (closeAfterHold) { closeAfterHold = false; aucOn = false; closePopup(); }
        if (setTile != NOBODY) setCall();
    }
    if (setT) setT--;
    if (pickT) pickT--;
    if (bidPop) bidPop--;
    // The auction's clock: a tick at going once, a tock at going twice.
    if (aucOn && game::phase() == P_AUCTION && auc.started && (auc.timer == 99 || auc.timer == 49))
        audio::sfx(auc.timer == 99 ? Sfx::Tick : Sfx::Tock);
    if (plT && plT < 8) plT++;
    if (tapT && ++tapT > 12) tapT = 0;
    if (growT && ++growT > 10) { growT = 0; growTile = NOBODY; }
    if (topT && topT < 40) topT++;
    for (uint8_t p = 0; p < SEATS; p++) {
        if (hudFlash[p]) hudFlash[p]--;
        int32_t d = cashTo[p] - sCash[p];
        if (d) sCash[p] += d / 5 ? d / 5 : (d > 0 ? 1 : -1);
    }

    // The whip zoom: one step per frame drawn; going out, the camera jumps
    // with each step rather than drifting between them.
    bool stepOut = false;
    if (tileH != zoomTo && zoomDrawn) {
        stepOut = tileH > zoomTo;
        setZoom((uint8_t)(tileH < zoomTo ? tileH + 1 : tileH - 1));
        zoomDrawn = false;
    }

    // The card on show turns up, waits, and turns away.
    if (popKind) {
        if (popClosing) { if (!popT || !--popT) { popKind = POP_NONE; popClosing = false; } }
        else if (popT < FLIP) popT++;
        else if (popKind == POP_CARD && !holdT && !waitPress) popClosing = true;
    }

    // The dice tumble in; where they stop, a knock and the doubles call.
    if (diceT && diceT <= diceFrames() && ++diceT > diceFrames()) {
        fx::shake(3, 1);
        fx::burst(fx::DUST, 96, 100, 8, 20, WHITE);
        audio::sfx(Sfx::Land);
        holdT = quick() ? 8 : 18;
        if (diceDouble) {
            fx::banner("DOUBLES!", fx::B_CYAN, 40, 50);
            audio::sfx(Sfx::Doubles);
            holdT = quick() ? 20 : 36;
        }
    }

    if (mv.on && ++mv.t >= hopFrames()) {
        mv.t = 0;
        mv.at = mv.jump ? mv.to : (uint8_t)((mv.at + mv.dir + TILES) % TILES);
        audio::sfx(mv.jump ? Sfx::Land : Sfx::Hop);
        if (mv.at == mv.to) {
            mv.on = 0;
            sPos[mv.seat] = mv.to;
        } else if (!mv.at && mv.dir > 0) {              // past GO
            fx::burst(fx::STAR, CX, CY - 10, 8, 30, GOLD);
        }
    }

    for (auto &f : flies) {
        if (!f.on || ++f.t < FLY_T) continue;
        f.on = 0;
        if (f.seat != NOBODY) { cashTo[f.seat] += f.amount; hudFlash[f.seat] = 12; }
    }

    // The end: closing time is called first; then the winner.
    if (over && !holdT && overT < 255) {
        overT++;
        if (overT == 1 && overWhy == BY_CLOSING) {
            fx::banner("CLOSING TIME", fx::B_CYAN, 40, 80);
            audio::sfx(Sfx::Doubles);
            overT = 2;
        } else if (overT == 1) overT = 80;
        if (overT == 80) {
            char buf[12];
            if (humans() == 1 && isHuman(winner)) fmtStr(buf, "YOU WIN!");
            else fmtStr(seatTag(buf, winner), " WINS!");
            fx::banner(buf, fx::B_RAINBOW, 40, 160);
            fx::fountain(fx::CONFETTI, 40, 90, 20);
            fx::fountain(fx::COIN, 88, 90, 14);
            audio::sfx(isHuman(winner) ? Sfx::Win : Sfx::Lose);
            audio::led(audio::LED_PARTY);
            focus = st.pl[winner].pos;
            zoomTo = fast ? 5 : 10;
        }
        if (overT == 230) overDone = true;
    }

    // What the camera frames: the token on the move, else the tile in focus.
    if (mv.on) {
        int x, y, z;
        moverPos(x, y, z);
        aimAt(x, y - zoomed(6), 2);
    } else aimTile(focus, 2);
    if (stepOut) snapCamera();
    stepCamera();

    // The glove glides to the deed being managed, or to the token whose
    // turn it is.
    int x, y;
    if (manageTile >= 0 || (pickT && pickAct == ACT_BUILD + 1)) {
        worldOf(focus, x, y);
        y -= zoomed(4);
    } else {
        uint8_t t = sPos[turn] == NOBODY ? st.pl[turn].pos : sPos[turn];
        slot(t, turn, x, y);
        y -= sized(TOKEN[turn][1] + 1);
    }
    gx16 += ((x << 4) - gx16) >> 1;
    gy16 += ((y << 4) - gy16) >> 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void drawToken(uint8_t seat, int x, int y, int lift, bool jailed) {
    const uint8_t *a = TOKEN[seat];
    int s = zscale(), sx = toScreenX(x), sy = toScreenY(y);
    if (sx < -24 || sx > 152 || sy < -8 || sy > 170) return;
    gfx_fillEllipse(sx, sy, sized(4), sized(1), INK);            // its shadow stays on the board
    if (seat == topSeat) {
        // Bankrupt: it topples, bouncing as it lands, and stays down.
        int e = fx::ease(fx::OUT_BOUNCE, topT, 30);
        spriteRot(a, a[0] / 2, a[1], sx, sy, (uint8_t)((60 * e) >> 8), s, RM_ID);
        return;
    }
    sprite4(a, sx - ((a[0] * s) >> 9), sy - ((a[1] * s) >> 8) - lift, RM_ID, s);
    if (jailed)
        for (int i = -1; i <= 1; i++)
            gfx_fillRect(sx + i * sized(4) - sized(1) / 2, sy - sized(12), sized(1), sized(12), INK);
}

static void drawBuilding(uint8_t t, const uint8_t *a, int along, bool growing) {
    int x, y, s = zscale();
    place(t, along, 2, x, y);
    // A new one drops in and bounces.
    if (growing) y -= sized(((256 - fx::ease(fx::OUT_BOUNCE, growT, 10)) * 10) >> 8);
    sprite4(a, toScreenX(x) - ((a[0] * s) >> 9), toScreenY(y) + sized(1) - ((a[1] * s) >> 8), RM_ID, s);
}

// What stands on a tile, back to front: on the near sides the houses are
// behind the tokens, on the far sides in front of them.
static void drawOn(uint8_t t) {
    bool near = side(t) == 0 || side(t) == 3;
    uint8_t lv = (uint8_t)(sDeed[t] >> 3);
    for (uint8_t pass = 0; pass < 2; pass++) {
        if ((pass == 0) == near) {
            // Houses along the band, the far end first.
            if (lv == 5) drawBuilding(t, HOTEL, 4, t == growTile);
            else for (uint8_t i = 0; i < lv; i++) {
                uint8_t k = side(t) < 2 ? (uint8_t)(lv - 1 - i) : i;
                drawBuilding(t, HOUSE, 1 + k * 2, t == growTile && k == lv - 1);
            }
        } else {
            for (uint8_t i = 0; i < SEATS; i++) {
                uint8_t seat = near ? i : (uint8_t)(3 - i);
                if (seat >= st.players || sPos[seat] != t) continue;
                int x, y;
                slot(t, seat, x, y);
                drawToken(seat, x, y, 0, t == JAIL_TILE && st.pl[seat].jail);
            }
        }
    }
}

static void drawStanding() {
    // By lattice depth: two tiles of the ring at each (one at the far and
    // near corners). The token on the move goes in after the tile it is
    // heading for.
    uint8_t next = mv.jump ? mv.to : (uint8_t)((mv.at + mv.dir + TILES) % TILES);
    for (int d = 0; d <= 20; d++) {
        uint8_t a = (uint8_t)((60 - d) % 40), b = (uint8_t)((20 + d) % 40);
        drawOn(a);
        if (b != a) drawOn(b);
        if (mv.on && (next == a || next == b)) {
            int x, y, z;
            moverPos(x, y, z);
            drawToken(mv.seat, x, y, z, false);
        }
    }
}

static void drawOwners() {
    for (uint8_t t = 0; t < TILES; t++)
        if ((sDeed[t] & 7) != BANK) rim(t, SEAT_COLOUR[sDeed[t] & 7]);
}

static void drawGlove(uint32_t frame) {
    if (!glove || mv.on || over || aucOn || popKind == POP_CARD) return;
    int x = toScreenX((int)(gx16 >> 4)), y = toScreenY((int)(gy16 >> 4));
    int bob = (fx::isin((int)(frame >> 3) * 40) * 2) >> 8;
    if (tapT) bob = (tapT < 6 ? tapT : 12 - tapT) / 2;
    sprite4(HAND, x - sized(HAND_TIP), y - sized(HAND[1]) + sized(bob) - 1, humanTurn ? RM_ID : RM_CPU, zscale());
}

// ---------------------------------------------------------------------------
// HUD: everyone's cash along the top; a plate at the foot of the screen
// naming where the play is.
// ---------------------------------------------------------------------------
static void plate(const char *a, uint8_t ca, const char *b, uint8_t cb, int y, int grow) {
    int wa = text35Width(a), tw = wa + (*b ? text35Width(b) + 1 : 0);
    int pw = ((tw + 8) * grow) >> 8;
    if (pw < 6) return;
    fillRound(64 - pw / 2, y, pw, 11, 2, NAVY);
    roundRect(64 - pw / 2, y, pw, 11, 2, GOLD);
    if (grow < 256) return;
    text35(64 - tw / 2, y + 3, a, ca);
    text35(64 - tw / 2 + wa + 1, y + 3, b, cb);
}

static void drawHud(uint32_t frame) {
    gfx_fillRect(0, 0, 128, 9, INK);
    gfx_hline(0, 9, 128, GOLD);
    for (uint8_t p = 0; p < st.players; p++) {
        char buf[12];
        int x = p * 32;
        bool lit = p == turn && !over;
        if (lit) gfx_fillRect(x, 0, 32, 9, NAVY);
        gfx_fillRect(x + 2, 2, 5, 5, hudFlash[p] & 2 ? WHITE : SEAT_COLOUR[p]);
        if (sCash[p] >= 10000) fmtStr(fmtInt(fmtStr(buf, "$"), sCash[p] / 1000), "K");   // (the slot is 32 px)
        else fmtMoney(buf, sCash[p]);
        text35(x + 9, 2, buf, hudFlash[p] & 2 ? SEAT_COLOUR[p] : lit ? FX_B : WHITE);
    }
    if (st.players < SEATS && phase() != P_OFF) {           // room for the round, and closing time
        char buf[12], *p = fmtInt(fmtStr(buf, "R"), st.round);
        if (st.roundCap) fmtInt(fmtStr(p, "/"), st.roundCap);
        text35(127 - text35Width(buf), 2, buf, st.round == st.roundCap ? RED : SILVER);
    }
    if (waitPress && !holdT && (frame & 32)) {              // blinking
        plate("PRESS A", WHITE, "", 0, 100, 256);
    } else if (plT && !barOn && !aucOn && !over && manageTile < 0) {
        char a[28];
        memcpy(a, plText, plSplit);
        a[plSplit] = 0;
        plate(a, plC[0], plText + plSplit, plC[1], 116, plT < 8 ? fx::ease(fx::OUT_BACK, plT, 8) : 256);
    }
}

// ---------------------------------------------------------------------------
// The dice, coins, the auction
// ---------------------------------------------------------------------------
static void drawDie(int x, int y, uint8_t face, uint8_t angle) {
    static const uint16_t PIPS[6] = {0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};     // a 3 x 3 of pips, bit = 3 * row + column
    if (angle) spriteRot(DIE, 6, 6, x, y, angle, 256, RM_ID);
    else sprite4(DIE, x - 6, y - 6, RM_ID);
    int cs = fx::isin(angle + 64), sn = fx::isin(angle);
    for (int i = 0; i < 9; i++) {
        if (!(PIPS[face - 1] >> i & 1)) continue;
        int dx = (i % 3 - 1) * 3, dy = (i / 3 - 1) * 3;
        gfx_fillRect(x + ((dx * cs - dy * sn + 128) >> 8) - 1, y + ((dx * sn + dy * cs + 128) >> 8) - 1, 2, 2, INK);
    }
}

static void drawDice() {
    if (!diceT || popKind || aucOn || over) return;
    int T = diceFrames(), t = diceT > T ? T : diceT;
    int drop = ((256 - fx::ease(fx::OUT_BOUNCE, t, T)) * 44) >> 8;
    for (int k = 0; k < 2; k++) {
        // In from the right, bouncing, spinning down to rest.
        int x = 88 + k * 18 + (T - t) * (3 + k), y = 96 + k * 7 - drop;
        uint8_t face = t < T ? (uint8_t)((t / 3 + k * 2) % 6 + 1) : (k ? diceB : diceA);
        gfx_fillEllipse(x, 96 + k * 7 + 7, 6, 2, INK);
        drawDie(x, y, face, (uint8_t)((T - t) * (k ? 19 : -23)));
    }
}

static void drawFlies() {
    for (auto &f : flies) {
        if (!f.on || f.t < 0) continue;
        int e = fx::ease(fx::OUT_CUBIC, f.t, FLY_T);
        int x = f.x0 + (((f.x1 - f.x0) * e) >> 8), y = f.y0 + (((f.y1 - f.y0) * e) >> 8);
        if (f.y0 < 12 && f.y1 < 12) y += (fx::isin(f.t * 128 / FLY_T) * 22) >> 8;     // slot to slot: a dip over the board
        gfx_fillRect(x - 1, y - 1, 3, 3, GOLD);
        gfx_pixel(x, y, WOOD);
    }
}

// The auction, beside the lot's deed: the price, the clock, and the bidders
// with the button each one taps.
static void drawAuction(uint32_t frame) {
    static const char *const TAP[4] = {" TAP A", " TAP PAD", " TAP B", " TAP SEL"};
    char buf[16];
    const int x = 68, w = 58, cx = x + w / 2;
    fillRound(x, 14, w, 90, 3, NAVY);
    roundRect(x, 14, w, 90, 3, GOLD);
    text35(cx - 13, 17, "AUCTION", GOLD);
    if (auc.leader == NOBODY) fmtStr(buf, "---");
    else fmtMoney(buf, auc.price);
    text35x2(cx - text35x2Width(buf) / 2, 26 - (bidPop > 3 ? 2 : 0), buf, bidPop ? WHITE : FX_B);
    // The clock: going, going...
    const char *call = "TAP TO BID";
    uint8_t c = SILVER;
    if (auc.started && game::phase() == P_AUCTION) {
        int left = auc.timer < AUCTION_WINDOW ? auc.timer : AUCTION_WINDOW;
        gfx_fillRect(x + 3, 41, (w - 6) * left / AUCTION_WINDOW, 3, left < 50 ? RED : CYAN);
        if (left < 100) { call = "GOING ONCE"; c = GOLD; }
        if (left < 50) { call = "GOING TWICE"; c = RED; }
    }
    text35(cx - text35Width(call) / 2, 47, call, c);
    // The bidders: on a player's lot the bank's opening bid first, then
    // everyone but the seller.
    uint8_t human = 0, row = 0;
    for (uint8_t p = auc.seller == BANK ? 0 : NOBODY; p != st.players; p++) {
        const char *tag = "";
        if (p < SEATS) {
            tag = isHuman(p) ? TAP[human++] : " CPU";
            if (p == auc.seller) continue;
        }
        int y = 57 + row++ * 11;
        bool leads = p == auc.leader || (p == NOBODY && auc.leader == BANK);
        if (leads) fillRound(x + 3, y - 2, w - 6, 10, 2, frame & 8 ? GOLD : FX_B);
        gfx_fillRect(x + 6, y, 5, 5, colourOf(p));
        if (p < SEATS) seatTag(buf, p);
        else fmtStr(buf, "BANK");
        fmtStr(buf + strlen(buf), leads ? " LEADS" : tag);
        text35(x + 14, y, buf, leads ? INK : p >= SEATS || canBid(p) || !auc.started ? WHITE : SILVER);
    }
}

// The jackpot, waiting over Free Parking's corner.
static void drawPot(uint32_t frame) {
    if (!st.pot || over || aucOn || popKind || manageTile >= 0) return;
    char buf[10];
    int x, y;
    worldOf(20, x, y);
    fmtMoney(buf, st.pot);
    int w = text35Width(buf) + 6, sx = toScreenX(x) - w / 2;
    int sy = toScreenY(y) - sized(18) + ((fx::isin((int)(frame >> 3) * 40) * 2) >> 8);
    if (sx < -w || sx > 128 || sy < 10 || sy > 100) return;
    fillRound(sx, sy, w, 9, 2, NAVY);
    roundRect(sx, sy, w, 9, 2, GOLD);
    text35(sx + 3, sy + 2, buf, GOLD);
}

void hud(uint32_t frame) { drawHud(frame); }

void renderScene(uint32_t) {
    drawTable();
    drawBoard();
    drawOwners();
    drawStanding();
}

// A still scene is not redrawn: the frame is flushed again, so palette
// effects keep moving at 60 Hz, and the bob steps at 7.5 Hz.
static uint32_t lastSig;

static uint32_t signature(uint32_t frame, uint32_t ui) {
    int lo, hi;
    bool rolling = false, flying = false;
    for (uint8_t p = 0; p < SEATS; p++) rolling |= sCash[p] != cashTo[p] || hudFlash[p];
    for (auto &f : flies) flying |= f.on != 0;
    if (fx::activeRows(lo, hi) || mv.on || rolling || flying || growT || setT || aucOn || (topT && topT < 40) ||
        (diceT && diceT <= diceFrames()) || (popKind && (popT < FLIP || popClosing)) || (plT && plT < 8))
        return frame;
    if (cx16 != (int32_t)aimX << 4 || cy16 != (int32_t)aimY << 4) return frame;
    uint32_t h = 2166136261u;
    uint32_t v[] = {
        (uint32_t)cam.x, (uint32_t)cam.y, tileH, focus, turn, glove, (uint32_t)(gx16 >> 4), (uint32_t)(gy16 >> 4),
        frame >> 3, tapT, plT, popKind, popA, popB, diceT, waitPress, barOn, over, ui, (uint32_t)manageTile, pickT,
    };
    for (uint32_t x : v) h = (h ^ x) * 16777619u;
    return h;
}

void invalidate() { lastSig = 0; }

bool render(uint32_t frame, uint32_t ui) {
    zoomDrawn = true;                            // the zoom may take its next step
    uint32_t sig = signature(frame, ui);
    if (sig == lastSig) return false;
    lastSig = sig;
    drawTable();
    drawBoard();
    drawOwners();
    if (manageTile >= 0 && (frame & 16)) tileTint((uint8_t)manageTile, FX_B);
    if (setT & 8) {
        uint8_t tiles[4], n = groupTiles(setGroup, tiles);
        for (uint8_t i = 0; i < n; i++) tileTint(tiles[i], FX_B);
    }
    drawStanding();
    drawPot(frame);
    drawGlove(frame);
    drawHud(frame);
    drawDice();
    if (popKind) {
        int open = fx::ease(fx::OUT_CUBIC, popT, FLIP);
        if (popKind == POP_DEED) cards::deed(34, 15, popA, open);
        else cards::card(64, 14, popA, popB, open);
    }
    if (manageTile >= 0) cards::deed(34, 15, (uint8_t)manageTile);
    if (aucOn) drawAuction(frame);
    drawFlies();
    fx::drawParticles((uint8_t)((2 * tileH + 2) / 5));
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(10, 127);
    return true;
}

}  // namespace stage
