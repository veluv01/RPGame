#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed (hot pixel loops live in the RPGame library, WheelStrip and RPGfx)
// The presenter (Presenter.h): the rules' events become the host's lines,
// turning panels, the spin, rolling money and banners; render() redraws only
// what changed (`rpgame redraw` checks it against a full redraw).
#include <RPGame.h>
#include <string.h>
#include "Presenter.h"
#include "Fx.h"
#include "Show.h"
#include "Spin.h"
#include "Layout.h"
#include "Stage.h"
#include "BoardView.h"
#include "WheelStrip.h"
#include "Sounds.h"

namespace present {

using namespace lay;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
// The board as shown: each panel's look, a countdown to its lighting up
// (0: none pending), and a timer for the look it is in.
static uint8_t look[pz::CELLS], lightIn[pz::CELLS], lookT[pz::CELLS];
static uint8_t drawn[pz::CELLS];        // what is on the screen (0xFF: draw it)
static uint8_t dings;                   // panels lit by this call: the pitch climbs
static uint8_t lastCursor = 0xFF;

static int32_t shown[3];                // the podiums' numbers, rolling to the rules'
static uint8_t podFlash[3];

enum View : uint8_t { V_BOARD, V_WHEEL };
static uint8_t view;
static spin::Spin wheel;
static int32_t wheelPos;                // Q8, carried from spin to spin
static int16_t wheelSpeed;              // px this tick
static bool spinning;
static bool spinPending;                // a spin waits for the host to finish his line
static bool hostSpin;                   // the final spin: the host's
static uint8_t spinStop, spinPower;
static uint8_t toBoard;                 // ticks until the camera goes back to the board
static uint8_t holdT;                   // a beat the rules wait out
static bool summaryOn;

// The host.
struct Said { uint8_t line, face, letter; int32_t number; };
static Said saidQ[4];
static uint8_t saidN;
static char bubText[80];
static uint8_t bubChars, bubLen, bubHold, face, blinkT = 90, blinking;
static bool bubOn;
static const uint8_t LINGER = 110;      // the bubble stays this long after the rules move on

// A line in the prompt bar: who is doing what.
static char cap[32];
static uint8_t capT;
static const uint8_t CAP_LINGER = 50;

static int8_t need = -1;
static bool quick, fast;
static uint8_t tickTock;

void hurry(bool on) { fast = on; }

int8_t takeNeed() {
    int8_t n = need;
    need = -1;
    return n;
}

static int32_t worth(const Show &s, uint8_t p) { return s.stepKind() == SK_ROUND ? s.cash[p] : s.total[p]; }
static int podX(uint8_t p) { return POD_X0 + POD_PITCH * p + POD_W / 2; }

static void caption(const Show &s, uint8_t p, const char *text, uint8_t beat = 40) {
    char nm[5];
    char *e = cap;
    if (p < 3) { e = fmtStr(e, s.name(p, nm)); e = fmtStr(e, text[0] == ' ' ? "" : ": "); }
    fmtStr(e, text);
    capT = (uint8_t)((quick ? beat / 2 : beat) + CAP_LINGER);
}

static void resetBoard(const Show &s) {
    for (uint8_t i = 0; i < pz::CELLS; i++) {
        char ch = s.puzzle.cell[i];
        look[i] = !pz::panel(i) ? board::NONE : !ch ? board::EMPTY
                  : s.puzzle.isShown(i) ? board::LETTER : board::BLANK;
        lightIn[i] = lookT[i] = 0;
    }
}

void reset(const Show &s) {
    resetBoard(s);
    for (uint8_t p = 0; p < 3; p++) { shown[p] = worth(s, p); podFlash[p] = 0; }
    view = V_BOARD;
    wheelPos = (spin::PEG / 2) << 8;            // at rest in the middle of a slot
    wheelSpeed = 0;
    spinning = spinPending = summaryOn = false;
    toBoard = holdT = capT = saidN = 0;
    bubOn = false;
    face = F_NORMAL;
    need = -1;
    invalidate();
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
static void light(const Show &s, char letter, uint8_t every) {
    uint8_t k = 0;
    dings = 0;
    for (uint8_t i = 0; i < pz::CELLS; i++)
        if (look[i] == board::BLANK && (!letter || s.puzzle.cell[i] == letter))
            lightIn[i] = (uint8_t)(1 + k++ * every);
}

// A banner and its sound. The rules wait it out: over the wheel that also
// times the cut back to the board.
static void splash(const char *text, uint8_t style, uint8_t frames, Sfx sound) {
    fx::banner(text, (fx::BannerStyle)style, view == V_WHEEL ? 84 : 74, frames);
    audio::sfx(sound);
    if (view == V_WHEEL) toBoard = (uint8_t)(frames + 5);
    else holdT = frames;
}

// "+$1,800" rising off a podium.
static void gain(uint8_t p, int32_t amount) {
    char buf[12];
    *fmtCash(fmtStr(buf, "+"), amount) = 0;
    fx::floatText(buf, podX(p), POD_Y - 8, GOLD);
    podFlash[p] = 40;
}

static void party(uint8_t coins) {
    fx::fountain(fx::COIN, 64, 114, coins);
    fx::fountain(fx::CONFETTI, 30, 100, 14);
    fx::fountain(fx::CONFETTI, 98, 100, 14);
    audio::led(audio::LED_PARTY);
}

static void landed(const Event &e) {
    char buf[12];
    static const char *const NAME[4] = {"FREE PLAY", "WILD CARD", "A TRIP!", "MYSTERY"};
    toBoard = quick ? 30 : 50;
    if (e.b == wedge::BANKRUPT || e.b == wedge::LOSE) return;      // their own events follow
    if (e.b >= wedge::FREE && e.b <= wedge::MYSTERY) {
        splash(NAME[e.b - wedge::FREE], e.b == wedge::WILD ? fx::B_RAINBOW : fx::B_CYAN, 50, Sfx::Envelope);
        return;
    }
    fmtCash(buf, e.amount);
    if (e.b == wedge::MONEY) splash(buf, fx::B_GOLD, quick ? 30 : 46, Sfx::Coin);
    else {                                                          // the top wedge, the $10,000 slot
        splash(buf, fx::B_RAINBOW, 70, Sfx::Solve);
        fx::fountain(fx::COIN, 64, 118, 10);
    }
}

void onEvents(Show &s) {
    Event e;
    char buf[8];
    quick = s.opt.pace != 0;
    while (s.popEvent(e)) {
        switch (e.type) {
            case Ev::NeedPuzzle: need = (int8_t)e.a; break;
            case Ev::PuzzleUp:
                resetBoard(s);
                for (uint8_t p = 0; p < 3; p++) shown[p] = worth(s, p);
                view = V_BOARD;
                summaryOn = false;
                toBoard = 0;
                audio::sfx(Sfx::Whoosh);
                invalidate();
                break;
            case Ev::TossPanel:
                if (e.a < pz::CELLS) { dings = (uint8_t)(s.puzzle.nLetters - s.puzzle.nHidden); lightIn[e.a] = 1; }
                break;
            case Ev::Buzz:
                audio::sfx(Sfx::BuzzIn);
                podFlash[e.a] = 40;
                caption(s, e.a, " BUZZES IN!", 50);
                break;
            case Ev::TurnTo:
                podFlash[e.a] = 20;
                if (!s.human(e.a) || s.humans() > 1) caption(s, e.a, " IS UP", 24);
                break;
            case Ev::Cursor:
                audio::sfx(Sfx::Cursor);
                if (bubOn && bubChars >= bubLen) bubOn = false;
                break;
            case Ev::Deny: {
                static const char *const WHY[3] = {"NO CONSONANTS LEFT", "NO VOWELS LEFT", "NOT ENOUGH CASH"};
                audio::sfx(Sfx::Deny);
                caption(s, 3, WHY[e.a < 3 ? e.a : 0], 10);
                break;
            }
            case Ev::Intent: {
                static const char *const SAY[7] = {"I'LL SPIN", "I'LL BUY A VOWEL", "I'D LIKE TO SOLVE",
                                                   "MY WILD CARD!", "I'LL KEEP THE CASH", "FLIP IT!", "PASS"};
                caption(s, e.a, SAY[e.b < 7 ? e.b : 0], 44);
                break;
            }
            case Ev::SpinStart:
                spinPending = true;
                hostSpin = e.a == HOST;
                spinStop = e.b;
                spinPower = e.c;
                break;
            case Ev::Landed: landed(e); break;
            case Ev::Bankrupt:
                splash("BANKRUPT", fx::B_RED, 90, Sfx::Bankrupt);
                pal::flash(NAVY, 0xE12, 14);        // the wall goes red
                if (e.amount) fx::fountain(fx::COIN, podX(e.a), 112, 12);
                podFlash[e.a] = 60;
                break;
            case Ev::LoseTurn: splash("LOSE A TURN", fx::B_WHITE, 60, Sfx::LoseTurn); break;
            case Ev::Called:
                light(s, (char)e.a, quick ? 6 : 10);
                if (e.amount) gain(e.c, e.amount);
                break;
            case Ev::NoLetter:
                buf[0] = 'N'; buf[1] = 'O'; buf[2] = ' '; buf[3] = (char)e.a; buf[4] = 0;
                splash(e.b ? "CALLED!" : buf, fx::B_RED, 44, Sfx::Buzzer);
                break;
            case Ev::Token:
                audio::sfx(Sfx::Coin);
                podFlash[e.a] = 30;
                break;
            case Ev::Mystery:
                if (e.b == 1) { splash("$10,000!", fx::B_RAINBOW, 90, Sfx::BigWin); party(12); }
                else if (e.b == 0) { gain(e.a, e.amount); audio::sfx(Sfx::Coin); }
                else audio::sfx(Sfx::Flip);
                break;
            case Ev::Type: audio::blip(e.b ? 2400 : 1500, 14); break;
            case Ev::SolveTry:
                if (!s.human(e.a)) caption(s, e.a, "I'D LIKE TO SOLVE", 50);
                audio::sfx(Sfx::Select);
                break;
            case Ev::SolveRight:
                light(s, 0, 2);
                bubOn = false;
                capT = 0;
                splash("SOLVED!", fx::B_RAINBOW, 85, Sfx::Solve);
                party(0);
                break;
            case Ev::SolveWrong:
                splash(e.b ? "TIME!" : "NO!", fx::B_RED, 44, e.b ? Sfx::TimeUp : Sfx::Buzzer);
                podFlash[e.a] = 40;
                break;
            case Ev::RoundWon:
                if (e.a < 3) {
                    gain(e.a, e.amount);
                    shown[e.a] = 0;                 // the podium counts the winnings up
                }
                break;
            case Ev::FinalBell: splash("FINAL SPIN", fx::B_CYAN, 90, Sfx::Bell); break;
            case Ev::Clock:
                audio::sfx((tickTock ^= 1) ? Sfx::Tick : Sfx::Tock);
                if (e.a <= 3) audio::blip(2600, 30);
                break;
            case Ev::Envelope: splash("YOUR ENVELOPE", fx::B_GOLD, 60, Sfx::Envelope); break;
            case Ev::Picked: audio::sfx(Sfx::Select); break;
            case Ev::Bonus:
                if (e.a) {
                    char big[12];
                    fmtCash(big, e.amount);
                    splash(big, fx::B_RAINBOW, 120, Sfx::BigWin);
                    party(14);
                } else {
                    light(s, 0, 2);
                    audio::sfx(Sfx::Lose);
                    holdT = 60;
                }
                break;
            case Ev::Say:
                if (saidN < 4) saidQ[saidN++] = {e.a, e.b, e.c, e.amount};
                break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------
static bool panelsMoving() {
    for (uint8_t i = 0; i < pz::CELLS; i++)
        if (lightIn[i] || (look[i] >= board::LIT && look[i] <= board::TURN2)) return true;
    return false;
}

static bool talking() { return bubOn && (bubChars < bubLen || bubHold > LINGER); }

void update(const Show &s) {
    quick = s.opt.pace != 0;

    // Panels: light up (a ding, climbing), hold, turn.
    for (uint8_t i = 0; i < pz::CELLS; i++) {
        if (lightIn[i]) {
            if (--lightIn[i]) continue;
            look[i] = board::LIT;
            lookT[i] = quick ? 8 : 14;
            uint16_t hz = (uint16_t)(1900 + dings * 140);
            audio::blip(hz > 3900 ? 3900 : hz, 36);
            if (dings < 30) dings++;
        } else if (look[i] >= board::LIT && look[i] <= board::TURN2 && !--lookT[i]) {
            look[i]++;
            lookT[i] = 2;
        }
    }

    // The wheel: on screen while a spin is charged, spun once the host has
    // said his piece.
    if (s.phase == Phase::Charge && view != V_WHEEL && !saidN && !talking()) {
        view = V_WHEEL;
        toBoard = 0;
        audio::sfx(Sfx::Whoosh);
        invalidate();
    }
    if (spinPending && !saidN && !talking() && !holdT) {
        spinPending = false;
        if (view != V_WHEEL) { view = V_WHEEL; audio::sfx(Sfx::Whoosh); invalidate(); }
        summaryOn = false;
        wheel.start(wheelPos, spinStop, spinPower, (int8_t)fx::rndRange(-3, 4), quick);
        spinning = true;
        toBoard = 0;
        capT = 0;
    }
    if (spinning) {
        int32_t before = wheelPos;
        for (uint8_t k = fast ? 3 : 1; k--;) wheel.step();
        wheelPos = wheel.pos();
        int32_t d = wheelPos - before;
        if (d < 0) d += spin::RIM << 8;
        wheelSpeed = (int16_t)(d >> 8);
        if ((before / (spin::PEG << 8)) != (wheelPos / (spin::PEG << 8)))
            audio::sfx((tickTock ^= 1) ? Sfx::Tick : Sfx::Tock);
        if (!wheel.moving()) {
            spinning = false;
            wheelSpeed = 0;
            holdT = quick ? 8 : 18;
            // Stopped a peg away from BANKRUPT: the audience lets its breath out.
            uint8_t side = (uint8_t)(spinStop % 3), w = (uint8_t)(spinStop / 3);
            if (side != 1 && s.wedgeAt(w).kind != wedge::BANKRUPT &&
                s.wedgeAt((uint8_t)((w + (side ? 1 : wedge::COUNT - 1)) % wedge::COUNT)).kind == wedge::BANKRUPT) {
                audio::sfx(Sfx::Ooh);
                holdT = 40;
            }
        }
    }
    if (toBoard && !--toBoard && view == V_WHEEL) {
        view = V_BOARD;
        invalidate();
    }
    if (holdT) holdT--;
    if (capT) capT--;

    // The podiums roll toward the rules' money.
    for (uint8_t p = 0; p < 3; p++) {
        int32_t d = worth(s, p) - shown[p];
        if (d) {
            int32_t step = d / 5;
            if (!step) step = d > 0 ? 1 : -1;
            shown[p] += step;
            if (d > 0 && (shown[p] & 3) == 0) audio::blip((uint16_t)(3000 + ((shown[p] * 7) & 511)), 8);
        }
        if (podFlash[p]) podFlash[p]--;
    }

    // The host: one line at a time, typed out, held a beat, then left up
    // until something else needs the space.
    if (saidN && !talking() && !spinning) {
        const Said &d = saidQ[0];
        const char *t = s.lineText(d.line, d.letter, d.number, bubText);
        if (t != bubText) { strncpy(bubText, t, sizeof bubText - 1); bubText[sizeof bubText - 1] = 0; }
        bubLen = (uint8_t)strlen(bubText);
        bubChars = 0;
        bubHold = (uint8_t)(LINGER + (quick ? 16 : 34));
        bubOn = true;
        face = d.face;
        memmove(saidQ, saidQ + 1, sizeof saidQ[0] * --saidN);
    }
    if (bubOn) {
        if (bubChars < bubLen) {
            bubChars = (uint8_t)(bubChars + (fast ? 3 : 1));
            if (bubChars > bubLen) bubChars = bubLen;
            if (bubChars & 1) audio::blip((uint16_t)(1900 + (bubChars * 97) % 700), 12);
        } else if (bubHold) {
            bubHold = (uint8_t)(bubHold > (fast ? 4 : 1) ? bubHold - (fast ? 4 : 1) : 0);
        } else { bubOn = false; face = F_NORMAL; }
    }
    if (blinking) blinking--;
    else if (--blinkT == 0) { blinking = 6; blinkT = (uint8_t)fx::rndRange(90, 220); }

    // The round's card comes up once the show has settled.
    bool card = s.phase == Phase::RoundEnd && s.stepKind() != SK_TOSS && !panelsMoving() && !holdT && !saidN &&
                !talking();
    if (card != summaryOn) { summaryOn = card; invalidate(); }

    fx::update();
}

// A human who can act is not kept waiting for the host to finish talking.
static bool acting(const Show &s) {
    if (!s.human(s.cur)) return false;
    switch (s.phase) {
        case Phase::TurnMenu: case Phase::Charge: case Phase::PickLetter: case Phase::MysteryChoice:
        case Phase::FinalMenu: case Phase::Entry: case Phase::Confirm: case Phase::BonusThink:
            return true;
        default: return false;
    }
}

bool busy(const Show &s) {
    bool speech = saidN || talking();
    bool beat = holdT || capT > CAP_LINGER;
    // A toss-up's panels turn on the rules' clock: a buzz must never be lost.
    if (s.phase == Phase::TossReveal) return speech || beat;
    return panelsMoving() || spinning || spinPending || toBoard || beat || (speech && !acting(s));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
struct Sig {
    uint32_t h = 2166136261u;
    void add(int32_t v) { h = (h ^ (uint32_t)v) * 16777619u; }
    void add(const char *s) { for (; *s; s++) add(*s); }
};

static uint32_t sigWall, sigMid, sigLow;
static bool forceAll = true;
static int16_t fxLo = 999, fxHi = -1;       // rows the transients touched last frame

void invalidate() { forceAll = true; }

static bool entering(const Show &s) {
    return s.human(s.cur) && (s.phase == Phase::Entry || s.phase == Phase::Confirm);
}

static bool picking(const Show &s) {
    return s.human(s.cur) && (s.phase == Phase::PickLetter || s.phase == Phase::Entry) && !busy(s);
}

static void clockText(char *buf, uint16_t ticks) {
    uint16_t sec = (uint16_t)((ticks + 59) / 60);
    char *p = fmtInt(buf, sec / 60);
    *p++ = ':';
    *p++ = (char)('0' + sec % 60 / 10);
    *p++ = (char)('0' + sec % 10);
    *p = 0;
}

static void statusText(const Show &s, char *left, char *right) {
    right[0] = 0;
    switch (s.stepKind()) {
        case SK_TOSS: fmtStr(left, "TOSS-UP"); fmtCash(right, s.tossWorth()); break;
        case SK_BONUS: fmtStr(left, "BONUS ROUND"); break;
        default:
            if (s.finalMode) fmtStr(left, "FINAL SPIN");
            else *fmtInt(fmtStr(left, "ROUND "), s.roundNo()) = 0;
    }
    if (s.phase == Phase::Entry || s.phase == Phase::Confirm || s.phase == Phase::FinalMenu) clockText(right, s.clock);
    else if (s.phase == Phase::BonusThink) clockText(right, s.think);
    else if (s.phase == Phase::PickLetter && s.stepKind() == SK_ROUND) {
        if (s.pickMode == PM_VOWEL) fmtStr(right, "VOWEL");
        else if (s.onFreePlay()) fmtStr(right, "FREE");
        else fmtCash(right, s.value);
    }
}

static void lowBand(const Show &s) {
    char buf[40], nm[5];
    if (picking(s)) {
        const char *hint = "A TYPE  B ERASE  SEL NEXT";
        if (s.phase == Phase::PickLetter) {
            hint = buf;
            if (s.stepKind() == SK_BONUS) {
                char *p = fmtStr(buf, "PICK ");
                p = fmtInt(p, s.nPicks + 1);
                p = fmtStr(p, " OF ");
                fmtInt(p, s.needPicks);
            } else if (s.pickMode == PM_VOWEL) fmtStr(buf, "VOWEL $250    B BACK");
            else if (s.pickMode == PM_ANY) fmtStr(buf, s.finalMode ? "ANY LETTER" : "FREE PLAY: ANY LETTER");
            else *fmtStr(fmtCash(buf, s.value), " A LETTER") = 0;
        }
        board::picker(s.pickAllowed(), s.puzzle.used, s.pickCur, hint);
        return;
    }
    if (view == V_BOARD) {
        uint8_t active = s.stepKind() == SK_TOSS && s.phase == Phase::TossReveal ? 3 : s.cur;
        for (uint8_t p = 0; p < 3; p++)
            board::podium(p, s.name(p, nm), shown[p], p == active,
                          (uint8_t)((s.wild[p] ? 1 : 0) | (s.prize[p] ? 2 : 0)), (podFlash[p] >> 2) & 1);
    }
    if (capT) { board::promptText(cap, WHITE); return; }
    if (spinning) {
        *fmtStr(fmtStr(buf, s.name(s.cur, nm)), s.name(s.cur, nm)[0] == 'Y' ? " SPIN" : " SPINS") = 0;
        board::promptText(hostSpin ? "THE FINAL SPIN" : buf, WHITE);
        return;
    }
    if (busy(s)) { board::promptClear(); return; }
    bool me = s.human(s.cur);
    switch (s.phase) {
        case Phase::TossReveal: {
            static const char *const HOW[4] = {"TOSS-UP", "ANY BUTTON TO BUZZ IN", "D-PAD        A OR B",
                                               "D-PAD   SELECT   A OR B"};
            board::promptText(HOW[s.humans()], WHITE);
            break;
        }
        case Phase::TurnMenu:
            if (me) {
                static const char *const M[4] = {"SPIN", "VOWEL $250", "SOLVE", "WILD"};
                board::promptMenu(M, s.menuCount(), s.menuSel);
            } else board::promptClear();
            break;
        case Phase::MysteryChoice:
            if (me) {
                *fmtCash(fmtStr(buf, "KEEP "), s.keepWorth()) = 0;
                const char *const M[2] = {buf, "FLIP IT"};
                board::promptMenu(M, 2, s.menuSel);
            } else board::promptClear();
            break;
        case Phase::FinalMenu:
            if (me) {
                static const char *const M[2] = {"SOLVE", "PASS"};
                board::promptMenu(M, 2, s.menuSel);
            } else board::promptClear();
            break;
        case Phase::Charge:
            if (s.stepKind() == SK_BONUS && !s.power) board::promptText("HOLD A TO SPIN", WHITE);
            else board::promptPower(s.power);
            break;
        case Phase::Confirm: board::promptText("A THAT'S IT    B EDIT", WHITE); break;
        case Phase::BonusThink: board::promptText(me ? "A  GOT IT!" : "", WHITE); break;
        default: board::promptClear();
    }
}

static void summaryCard(const Show &s) {
    char title[14], line[28], nm[3][5];
    const char *names[3];
    int32_t won[3] = {0, 0, 0};
    for (uint8_t p = 0; p < 3; p++) {
        const char *n = s.name(p, nm[p]);
        if (n != nm[p]) strcpy(nm[p], n);
        names[p] = nm[p];
    }
    if (s.stepKind() == SK_BONUS) {
        fmtStr(title, "BONUS ROUND");
        fmtStr(line, s.wonAmount ? "WHAT A FINISH!" : "NOT THIS TIME");
    } else {
        *fmtInt(fmtStr(title, "ROUND "), s.roundNo()) = 0;
        *fmtStr(fmtStr(line, names[s.winner < 3 ? s.winner : 0]), " SOLVED IT!") = 0;
    }
    if (s.winner < 3) won[s.winner] = s.wonAmount;
    board::summary(title, line, names, won, s.total, s.winner, s.humans() ? "A  CONTINUE" : "");
}

bool render(const Show &s, uint32_t frame) {
#ifdef CHSIM_FORCE_FULL
    forceAll = true;                                        // the redraw check's reference build (rpgame redraw)
#endif
    static_assert(F_RAISED == stage::E_RAISED && F_SMILE == stage::E_SMILE && F_SURPRISED == stage::E_SURPRISED,
                  "a Face is an Expr");
    uint8_t expr = face;
    if (bubOn && bubChars < bubLen && ((frame >> 2) & 1)) expr = stage::E_TALK;
    if (blinking) expr = stage::E_BLINK;

    // Rows the transients (particles, floats, a banner) touch now
    // or touched last frame: whatever lies under them is drawn again.
    int lo, hi;
    if (!fx::activeRows(lo, hi)) { lo = 999; hi = -1; }
    int a = lo < fxLo ? lo : fxLo, b = hi > fxHi ? hi : fxHi;
    fxLo = (int16_t)lo; fxHi = (int16_t)hi;
    #define HIT(y0, y1) (a <= (y1) && b >= (y0))
    bool drew = false;

    // The wall.
    char left[14], right[10];
    statusText(s, left, right);
    Sig w;
    w.add(expr); w.add(view); w.add(bubOn); w.add(bubChars); w.add((int32_t)s.puzzle.used); w.add(s.opt.host);
    w.add(left); w.add(right); w.add(s.wild[0] | s.wild[1] | s.wild[2]);
    if (forceAll || w.h != sigWall || HIT(0, WALL_H - 1)) {
        dbg::profStart();
        stage::wall();
        stage::dealer(expr, view == V_WHEEL ? 2 : 1, s.opt.host != 0);
        if (bubOn) stage::speechBubble(bubText, bubChars);
        else stage::rack(s.puzzle.used, left, right, s.wild[0] || s.wild[1] || s.wild[2],
                         s.clock < 300 && (s.clock & 16) && entering(s));
        sigWall = w.h;
        drew = true;
        dbg::prof(0);
    }

    // The card.
    if (summaryOn) {
        Sig m;
        m.add(0x5C); m.add(s.winner); m.add(s.wonAmount);
        if (forceAll || m.h != sigMid || HIT(STRIP_Y, 127)) {
            summaryCard(s);
            sigMid = m.h; sigLow = 0;
            drew = true;
        }
        forceAll = false;
        return drew;
    }

    // The wheel, or the strip and the board.
    Sig m;
    if (view == V_WHEEL) {
        m.add(0x3E); m.add(wheelPos >> 8); m.add(wheelSpeed >= 6 ? 1 + (wheelSpeed >= 10) + (int32_t)(frame & 1) * 4 : 0);
        m.add(s.layout()); m.add((int32_t)s.stepKind());
        if (forceAll || m.h != sigMid || HIT(RIM_Y, WHEEL_Y1)) {
            dbg::profStart();
            wheelstrip::draw(s, wheelPos, wheelSpeed, frame);
            drew = true;
            dbg::prof(1);
        }
    } else {
        bool entry = entering(s);
        uint8_t cursor = entry ? s.blank : 0xFF;
        m.add(0xB0); m.add(s.puzzle.category);
        bool all = forceAll || m.h != sigMid || HIT(STRIP_Y, BOARD_Y + BOARD_H - 1) || cursor != lastCursor;
        dbg::profStart();
        if (all) {
            board::strip(s.puzzle.category);
            board::frame();
            memset(drawn, 0xFF, sizeof drawn);
            lastCursor = cursor;
        }
        for (uint8_t i = 0; i < pz::CELLS; i++) {
            uint8_t lk = look[i];
            char ch = s.puzzle.cell[i];
            if (lk == board::BLANK && entry && s.guess[i]) { lk = board::GUESS; ch = s.guess[i]; }
            uint8_t code = (uint8_t)((lk << 5) ^ ch);
            if (code == drawn[i]) continue;
            drawn[i] = code;
            board::cell(i, lk, ch, i == cursor);
            drew = true;
        }
        if (all && cursor != 0xFF) board::cursorBox(cursor);
        dbg::prof(1);
    }
    sigMid = m.h;

    // Podiums, prompt bar, picker.
    Sig q;
    bool pick = picking(s);
    q.add(view); q.add(pick); q.add((int32_t)s.phase); q.add(s.menuSel); q.add(s.cur); q.add(busy(s));
    q.add(capT ? 1 : 0); if (capT) q.add(cap);
    q.add(spinning);
    if (pick) { q.add(s.pickCur); q.add((int32_t)s.pickAllowed()); q.add(s.pickMode); q.add(s.nPicks); }
    if (s.phase == Phase::Charge) q.add(s.power);
    if (view == V_BOARD)
        for (uint8_t p = 0; p < 3; p++) {
            q.add(shown[p]); q.add(podFlash[p] >> 2 & 1); q.add(s.wild[p]); q.add(s.prize[p]); q.add(s.kind[p]);
        }
    if (forceAll || q.h != sigLow || HIT(view == V_BOARD || pick ? POD_Y : PROMPT_Y, 127)) {
        dbg::profStart();
        lowBand(s);
        sigLow = q.h;
        drew = true;
        dbg::prof(2);
    }
    forceAll = false;
    return drew;
    #undef HIT
}

void overlay(const Show &s, uint32_t frame) {
    (void)s; (void)frame;
    fx::drawParticles();
    fx::drawFloats();
    fx::drawBanner();
}

}  // namespace present
