#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// The presenter (Presenter.h): the rules' events in, the caller, the
// carousel, the glove and the banners out, and the play screen drawn in
// bands that repaint only when something in them changed.
#include <RPGame.h>
#include <string.h>
#include "Presenter.h"
#include "Fx.h"
#include "Bingo.h"
#include "Layout.h"
#include "Table.h"
#include "Cards.h"
#include "src/assets/Assets.h"
#include "Sounds.h"

namespace present {

using namespace lay;

enum Face : uint8_t { F_NORMAL, F_ANGRY, F_RAISED, F_SMILE, F_SURPRISED };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int32_t shown;                   // the plaque's purse, rolling to the real one
static uint8_t purseFlash;

// The caller.
static char bubText[40];
static uint8_t bubChars, bubLen, bubHold, face, faceT, blinkT = 90, blinking, look = 1;
static bool bubOn, bubBig;

// The carousel: how far the cards still have to slide (pixels, + = from the right).
static int16_t scroll;

// A daub's pop, a refused press, the winning line.
static uint32_t flashCells, winCells;
static uint8_t flashCard, flashT, denyT, winCard;

// CHChess's glove: it points at what A will act on (the newest card at the
// buy-in, the waiting number on the card in play) and dips when A lands.
// Q4 position gliding to its target.
static int16_t gx16, gy16;
static uint8_t tapT;
// A daub's follow-through: the glove stays on the cell, presses in, kicks
// back up and settles before it moves on (offsets in px, + = down).
static const int8_t RECOIL[] = {1, 2, 3, 3, 2, 0, -2, -3, -4, -4, -4, -3, -3, -2, -2, -1, -1, -1, 0, 0, 0, 0};
static const uint8_t HOLD = sizeof RECOIL;
static uint8_t holdT;                   // ticks into the follow-through, 0 = none
static bool gloveOn;

// The end of a round: what follows the first banner.
enum After : uint8_t { A_NONE, A_JACKPOT, A_CORRECT, A_RIVAL };
static uint8_t after[2];
static uint8_t veilT;
static bool rivalHad;

// The caller's nicknames for some numbers (11 characters at most).
static const struct { uint8_t n; const char *text; } LINGO[] = {
    {1, "KELLY'S EYE"}, {7, "LUCKY SEVEN"}, {11, "LEGS ELEVEN"}, {13, "UNLUCKY!"}, {16, "SWEET 16"},
    {21, "ROYAL 21"}, {22, "TWO DUCKS"}, {33, "ALL THE 3S"}, {44, "ALL THE 4S"}, {50, "HALF WAY"},
    {55, "ALL THE 5S"}, {66, "CLICKETY!"}, {75, "TOP SHELF"},
};

static uint8_t exprFor(uint8_t f) {
    static const uint8_t E[5] = {table::E_NORMAL, table::E_ANGRY, table::E_RAISED, table::E_SMILE, table::E_SURPRISED};
    return f < 5 ? E[f] : table::E_NORMAL;
}

void say(const char *text, uint8_t f, uint8_t hold) {
    strncpy(bubText, text, sizeof bubText - 1);
    bubText[sizeof bubText - 1] = 0;
    bubLen = (uint8_t)strlen(bubText);
    bubChars = 0; bubHold = hold; bubOn = true; bubBig = false; face = f;
}

void dismissBubble() { if (bubOn) { bubOn = false; face = F_NORMAL; } }

void reset(const Bingo &g) {
    shown = g.purse; purseFlash = 0;
    bubOn = false; face = F_NORMAL;
    scroll = 0;
    flashT = denyT = 0; winCells = 0;
    after[0] = after[1] = A_NONE; veilT = 0;
    gloveOn = false; tapT = holdT = 0;
    invalidate();
}

static void queue(uint8_t a) {
    if (!after[0]) after[0] = a; else after[1] = a;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
void onEvents(Bingo &g) {
    Event e;
    char buf[16];
    while (g.popEvent(e)) {
        switch (e.type) {
            case Ev::BuyOpen:
                winCells = 0; scroll = 0;
                say(g.stats.rounds ? "ANOTHER\nROUND?" : "WELCOME!\nHOW MANY\nCARDS?", F_SMILE, 70);
                invalidate();
                break;
            case Ev::BuyPick: tapT = 1; audio::sfx(Sfx::Cursor); break;
            case Ev::Deny: audio::sfx(Sfx::Deny); break;
            case Ev::Start:
                say("EYES\nDOWN!", F_SMILE, 50);
                tapT = 1;
                audio::sfx(Sfx::Chip);
                invalidate();
                break;
            case Ev::Resume:
                say("WHERE\nWERE WE?", F_RAISED, 80);
                invalidate();
                break;
            case Ev::Call: {
                char *p = callName(bubText, e.a);
                for (auto &l : LINGO) if (l.n == e.a) { *p++ = '\n'; p = fmtStr(p, l.text); }
                *p = 0;
                bubLen = (uint8_t)strlen(bubText);
                bubChars = 0; bubHold = 60; bubOn = bubBig = true; face = F_NORMAL;
                audio::sfx(Sfx::Ball);
                if (e.b) audio::led(audio::LED_BLINK);      // it is on one of the player's cards
                break;
            }
            case Ev::Focus:
                scroll = (int16_t)(scroll + (e.b ? CARD_PITCH : -CARD_PITCH));
                if (scroll > 2 * CARD_PITCH - 8) scroll = 2 * CARD_PITCH - 8;
                if (scroll < -(2 * CARD_PITCH - 8)) scroll = -(2 * CARD_PITCH - 8);
                denyT = 0;
                holdT = 0;                              // a swipe cuts the follow-through short
                audio::sfx(Sfx::Cursor);
                break;
            case Ev::Daub: {
                flashCard = e.a; flashCells = (uint32_t)e.amount; flashT = 10;
                holdT = 1;
                for (uint8_t i = 0; i < 25; i++) {
                    if (!((e.amount >> i) & 1)) continue;
                    int x, y;
                    cards::cellXY(i, x, y);
                    fx::gack(x, y, e.c ? 7 : 5, cards::daub(g));   // a fast daub splats harder
                }
                fx::shake(4, 1);
                audio::sfx(e.c ? Sfx::Coin : Sfx::Chip);
                if (e.c && g.streak >= 2) {
                    *fmtInt(fmtStr(buf, "x"), g.streak) = 0;
                    fx::floatText(buf, CARD_X + CARD_W / 2, CARD_Y + 16, GOLD);
                }
                break;
            }
            case Ev::Miss:
                denyT = 12;
                audio::sfx(Sfx::Deny);
                break;
            case Ev::Power:
                fx::floatText("POWER!", 77, BAR_Y - 2, CYAN);
                audio::sfx(Sfx::Power);
                break;
            case Ev::PowerUse:
                audio::sfx(Sfx::Power);
                if (e.a == P_WILD) {
                    flashCard = e.b; flashCells = (uint32_t)e.amount; flashT = 16;
                    for (uint8_t i = 0; i < 25; i++) {
                        if (!((e.amount >> i) & 1)) continue;
                        int x, y;
                        cards::cellXY(i, x, y);
                        fx::burst(fx::STAR, x, y, 8, 26, FX_A);
                    }
                    fx::floatText("WILD!", CARD_X + CARD_W / 2, CARD_Y + 16, CYAN);
                } else if (e.a == P_FREEZE) {
                    say("TIME\nOUT!", F_RAISED, 60);
                } else {
                    fx::floatText("2X POT", CARD_X + CARD_W / 2, CARD_Y + 16, GOLD);
                }
                break;
            case Ev::Bingo:
                // The call: "BINGO!" - or, now and then, one word too many.
                winCard = e.a; winCells = LINES[e.b]; scroll = 0;
                dismissBubble();
                face = F_SURPRISED; faceT = 120;
                fx::banner((e.c & WIN_RARE) ? "IT'S A BINGO!" : "BINGO!", fx::B_RAINBOW, BANNER_Y, 120);
                fx::fountain(fx::CONFETTI, 40, 104, 20);
                fx::fountain(fx::CONFETTI, 88, 104, 20);
                fx::fountain(fx::COIN, 64, 104, 8);
                fx::shake(6, 2);
                audio::sfx(Sfx::BigWin);
                audio::led(audio::LED_PARTY);
                veilT = 120;
                purseFlash = 24;
                after[0] = after[1] = A_NONE;
                if (e.c & WIN_JACKPOT) queue(A_JACKPOT);
                if (e.c & WIN_RARE) queue(A_CORRECT);
                invalidate();
                break;
            case Ev::Rival:
                *fmtStr(fmtInt(fmtStr(buf, "TABLE "), e.a), "!") = 0;
                {
                    char line[28];
                    *fmtStr(fmtStr(line, "BINGO AT\n"), buf) = 0;
                    say(line, F_SMILE, 60);
                }
                fx::floatText("BINGO!", 106, BAR_Y - 6, WHITE);   // from somewhere across the hall
                audio::sfx(Sfx::Lose);
                rivalHad = e.b != 0;
                after[0] = A_RIVAL; after[1] = A_NONE;
                break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------
// Where the glove's fingertip goes, or false: no glove.
static bool gloveTarget(const Bingo &g, int &x, int &y) {
    if (veilT || after[0]) return false;
    if (g.phase == Phase::Buy) {
        x = cards::buyX((uint8_t)(g.buyN ? g.buyN - 1 : 0)) + 5;
        y = BUY_Y - 4;
        return true;
    }
    if (g.phase != Phase::Calling) return false;
    uint32_t m = g.pend[g.focus];
    if (m) {                                            // the first number waiting here
        uint8_t i = 0;
        while (!((m >> i) & 1)) i++;
        cards::cellXY(i, x, y);
        y -= 2;
    } else {
        bool other = false;
        for (uint8_t k = 0; k < g.nCards; k++) if (g.pend[k]) other = true;
        if (other) { x = CARD_X + CARD_PITCH + 9; y = CARD_Y + 30; }   // over there: swipe
        else { x = CARD_X + CARD_W + 2; y = CARD_Y + CARD_H - 4; }   // nothing to do: at rest
    }
    x += scroll;                                        // it rides the slide
    return true;
}

void update(const Bingo &g) {
    // The cards cover half of what is left of their slide every tick.
    if (scroll) scroll = (int16_t)(scroll / 2);
    if (flashT) flashT--;
    if (denyT) denyT--;
    if (veilT) veilT--;

    // What follows the round's first banner, one thing at a time.
    if (after[0] && !fx::bannerActive() && !(bubOn && bubChars < bubLen)) {
        uint8_t a = after[0];
        after[0] = after[1]; after[1] = A_NONE;
        if (a == A_JACKPOT) {
            fx::banner("JACKPOT!", fx::B_GOLD, BANNER_Y, 100);
            fx::fountain(fx::COIN, 40, 104, 12);
            fx::fountain(fx::COIN, 88, 104, 12);
            audio::sfx(Sfx::BigWin);
            veilT = 100;
        } else if (a == A_CORRECT) {
            say("You just say\nbingo....", F_RAISED, 130);
        } else {
            fx::banner(rivalHad ? "TOO SLOW!" : "NO LUCK!", fx::B_RED, BANNER_Y, 70);
            veilT = 70;
        }
    }

    // The purse rolls toward the real one.
    int32_t d = g.purse - shown;
    if (d) {
        int32_t step = d / 5;
        if (!step) step = d > 0 ? 1 : -1;
        shown += step;
        if (d > 0 && (shown & 3) == 0) audio::blip((uint16_t)(3000 + ((shown * 7) & 511)), 8);
    }
    if (purseFlash) purseFlash--;

    // Speech bubble typewriter, the caller's face.
    if (bubOn) {
        if (bubChars < bubLen) {
            bubChars++;
            if (bubChars & 1) audio::blip((uint16_t)(1900 + (bubChars * 97) % 700), 12);
        } else if (bubHold) bubHold--;
        else { bubOn = false; face = F_NORMAL; }
    } else if (faceT && !--faceT) face = F_NORMAL;
    if (blinking) blinking--;
    else if (--blinkT == 0) { blinking = 6; blinkT = (uint8_t)fx::rndRange(90, 220); }
    look = scroll > 4 ? 2 : (scroll < -4 ? 0 : 1);

    int tx, ty;
    bool show = gloveTarget(g, tx, ty);
    if (show && !gloveOn) { gx16 = (int16_t)(tx << 4); gy16 = (int16_t)(ty << 4); }   // appears in place
    gloveOn = show;
    if (!show) holdT = 0;
    if (holdT && ++holdT > HOLD) holdT = 0;
    if (show && !holdT) {                               // (held on the daub until its follow-through ends)                                         // a quarter of the way there each tick
        int dx = (tx << 4) - gx16, dy = (ty << 4) - gy16;
        gx16 = (int16_t)(gx16 + (dx / 4 ? dx / 4 : dx));
        gy16 = (int16_t)(gy16 + (dy / 4 ? dy / 4 : dy));
    }
    if (tapT && ++tapT > 8) tapT = 0;

    fx::update();
}

bool busy() {
    return fx::bannerActive() || veilT || after[0] || bubOn;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
struct Sig {
    uint32_t h = 2166136261u;
    void add(int32_t v) { h = (h ^ (uint32_t)v) * 16777619u; }
};

static uint32_t sigWall = 0, sigTable = 0;
static bool forceAll = true;

void invalidate() { forceAll = true; }

static int gloveBob(uint32_t frame) { return (fx::isin((int)((frame >> 3) * 40)) + 128) >> 8; }
static int gloveDip() { return holdT ? RECOIL[holdT - 1] : tapT ? (tapT < 4 ? tapT : 8 - tapT) >> 1 : 0; }

static bool buying(const Bingo &g) { return g.phase == Phase::Buy || g.phase == Phase::Welcome; }

static void felt() {
    gfx_fillRect(0, TABLE_Y, 128, BAR_Y - TABLE_Y, FELT);
    dither(0, TABLE_Y, 128, 2, FELT_DK, 0);
    dither(0, BAR_Y - 2, 128, 2, FELT_DK, 0);
}

// The card in play and what shows of its neighbours, mid-slide or at rest.
static void carousel(const Bingo &g) {
    int n = g.nCards;
    for (int slot = -2; slot <= 2; slot++) {
        if (n == 1 && slot) continue;
        int k = (((g.focus + slot) % n) + n) % n;
        cards::Look lk;
        lk.focused = slot == 0;
        lk.flash = (flashT && flashCard == k) ? flashCells : 0;
        lk.win = (winCells && winCard == k) ? winCells : 0;
        lk.deny = slot == 0 && (denyT & 4);
        cards::draw(g, (uint8_t)k, CARD_X + slot * CARD_PITCH + scroll, lk);
    }
}

static void wallBand(const Bingo &g, uint8_t expr) {
    table::wall();
    table::dealer(expr, look, g.opt.dealer != 0);
    if (bubOn) table::speechBubble(bubText, bubChars, bubBig);
    else table::plaque(shown, buying(g) ? g.potFor(g.buyN) : g.pot, purseFlash);
    table::tote(g.balls, buying(g) ? 0 : g.nCalled);
    table::rail();
}

bool render(const Bingo &g, uint32_t frame) {
#ifdef CHSIM_FORCE_FULL
    forceAll = true;                                        // the redraw check's reference build (rpgame redraw)
#endif
    uint8_t expr = exprFor(face);
    if (bubOn && bubChars < bubLen && ((frame >> 2) & 1)) expr = table::E_TALK;
    if (blinking) expr = table::E_BLINK;

    int lo, hi;
    bool moving = fx::activeRows(lo, hi);
    bool low = moving && hi >= BAR_Y - 2;                   // something over the bar
    bool buy = buying(g);
    int32_t pot = buy ? g.potFor(g.buyN) : g.pot;

    // Four parts, each redrawn only when what it shows changed or something
    // moving touched it this frame or the last: the wall, the plaque in it
    // (the purse rolls on its own), the felt, the bar.
    Sig w;
    w.add(expr); w.add(look); w.add(bubOn); w.add(bubChars); w.add(bubBig);
    w.add(buy ? 0 : g.nCalled); w.add(g.opt.dealer);
    Sig q;
    q.add(shown); q.add(purseFlash ? 1 + ((purseFlash >> 2) & 1) : 0); q.add(pot);
    Sig t;
    t.add((int32_t)g.phase); t.add(g.focus); t.add(g.nCards); t.add(scroll); t.add(g.doubleCard);
    t.add(flashT != 0); t.add(denyT & 4); t.add((int32_t)winCells); t.add(veilT != 0);
    if (buy) { t.add(g.buyN); t.add(g.purse); t.add(g.opt.stakes); t.add(g.opt.hall); }
    else for (uint8_t k = 0; k < g.nCards; k++) { t.add((int32_t)g.daub[k]); t.add((int32_t)g.pend[k]); }
    t.add(gloveOn); t.add(gx16 >> 4); t.add(gy16 >> 4); t.add(gloveBob(frame)); t.add(gloveDip());
    Sig b;
    b.add(buy); b.add(buy ? g.buyN : g.nCards); b.add(g.focus); b.add(g.power); b.add(g.meter);
    b.add(g.jackpot); b.add(g.doubleCard); b.add(g.frozen());
    if (!buy) for (uint8_t k = 0; k < g.nCards; k++) b.add(g.pend[k] != 0);

    static bool wasMoving = false, wasWallFx = false, wasLow = false;
    static uint32_t sigPlaque = 0, sigBar = 0;
    bool drawTable = forceAll || t.h != sigTable || moving || wasMoving;
    wasMoving = moving;
    bool wallFx = moving && lo < TABLE_Y;
    bool drawWall = forceAll || w.h != sigWall || wallFx || wasWallFx;
    wasWallFx = wallFx;
    bool drawPlaque = !drawWall && !bubOn && q.h != sigPlaque;
    bool drawBar = forceAll || b.h != sigBar || low || wasLow;
    wasLow = low;
    forceAll = false;
    sigWall = w.h; sigTable = t.h; sigPlaque = q.h; sigBar = b.h;

    dbg::profStart();
    if (drawWall) wallBand(g, expr);
    else if (drawPlaque) table::plaque(shown, pot, purseFlash);
    dbg::prof(0);
    if (drawBar) cards::bar(g, g.frozen());
    if (drawTable) {
        gfx_setClip(0, TABLE_Y, 128, BAR_Y - TABLE_Y);
        felt();
        if (buy) cards::buyIn(g);
        else carousel(g);
        if (veilT) {                                        // the band a banner stands on
            gfx_fillRect(0, STRIP_Y, 128, STRIP_H, INK);
            gfx_hline(0, STRIP_Y, 128, GOLD);
            gfx_hline(0, STRIP_Y + STRIP_H - 1, 128, GOLD);
        }
        gfx_resetClip();
        dbg::prof(1);
    }
    return drawWall || drawPlaque || drawTable || drawBar;
}

void overlay(const Bingo &g, uint32_t frame) {
    if (gloveOn)
        sprite4(HAND, (gx16 >> 4) - HAND_TIP, (gy16 >> 4) - 15 + gloveBob(frame) + gloveDip(), cards::cuff(cards::daub(g)));
    fx::drawParticles(2);
    fx::drawFloats();
    fx::drawBanner();
    fx::applyShake(TABLE_Y, 127);
    dbg::prof(2);
}

}  // namespace present
