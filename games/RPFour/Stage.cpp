// The play screen in motion (see Stage.h): the disc hovering and falling,
// the dealer talking, the camera, the endings, and drawing it all.
#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Mask)
#include <string.h>
#include <RPGame.h>
#include <Arduino.h>
#include "config.h"
#include "Stage.h"
#include "Game.h"
#include "Table.h"
#include "Fx.h"
#include "Sounds.h"
#include "src/assets/Assets.h"

namespace stage {

using namespace table;

Plaque plaque = {"", "", "", 0, 0};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static c4::Board shown;              // the discs at rest
static uint8_t turnSide;
static bool humanTurn, live, fast;
static uint8_t cur = 3;
static int16_t hoverX;               // the hovering disc's world x, in 1/16ths
static uint8_t denyT;

struct Fall { bool on; uint8_t side, col, row, t, t1, amp; int16_t y1; };
static Fall fall;

// The dealer.
static char talk[taunt::LINE_MAX];
static uint8_t talkLen, talkFace;
static uint16_t talkT;               // ticks since he began the line
static bool talkWait;                // the game waits for this one
static uint8_t blinkT;
static uint8_t blinkIn = 120;
static const uint8_t TALK_HOLD = 40; // ticks a line the game waits for stays up before play goes on
static const uint16_t TALK_UP = 190; // ... and before the bubble comes down

// The end of the game.
enum Phase : uint8_t { PLAY, HOLD, ZOOM, LIGHT, ADMIRE, FADE, SCENE, END };
static uint8_t phase;
static uint16_t phaseT;
static uint8_t winnerSide, endFace, nLit;
static c4::Bits lit;
static char endLine[taunt::LINE_MAX];
static bool overDone, skip;
static uint8_t zoomTo = 5;
static bool zoomDrawn = true;
static int16_t focusX = CX, focusY = CY;
static uint8_t tickT;

static bool vsCpu() { return game::setup.mode == game::VS_CPU; }
static bool fourKnown() { return winnerSide != c4::NOBODY && game::four[0] != 0xFF; }
static bool youWon() { return winnerSide == game::YOU; }
static bool talking() { return talkLen && talkT < talkLen; }
static bool bubbleUp() { return talkLen && talkT < talkLen + TALK_UP; }

// ---------------------------------------------------------------------------
// Public controls
// ---------------------------------------------------------------------------
uint8_t cursor() { return cur; }
void setCursor(uint8_t col) { cur = col; }
void setFast(bool on) { fast = on; }
void deny() { denyT = 14; audio::sfx(Sfx::Deny); }
void hurry() { skip = true; }
bool overShown() { return overDone; }
bool ending() { return phase == SCENE; }

bool busy() {
    return fall.on || phase != PLAY || (talkLen && talkWait && talkT < talkLen + (fast ? 14 : TALK_HOLD));
}

bool ready() { return live && !fall.on && phase == PLAY; }

void begin() {}

void reset() {
    memset(&fall, 0, sizeof fall);
    phase = PLAY;
    phaseT = 0;
    talkLen = 0;
    lit = 0; nLit = 0;
    overDone = skip = false;
    live = false;
    zoom = zoomTo = 5;
    zoomDrawn = true;
    setCamera(CX, CY);
    pal::setFade(16);
    fx::clear();
    invalidate();
}

void demo() {
    // A game in full swing, for the camera to drift over.
    static const char MOVES[] = "4435362552364717621";
    reset();
    c4::reset(shown);
    uint8_t s = 0;
    for (const char *m = MOVES; *m; m++, s ^= 1) c4::play(shown, s, (uint8_t)(*m - '1'));
}

// ---------------------------------------------------------------------------
// The game's events
// ---------------------------------------------------------------------------
static void speak(const char *text, uint8_t face, bool wait) {
    talkLen = (uint8_t)(fmtStr(talk, text) - talk);
    talkFace = face;
    talkT = 0;
    talkWait = wait;
}

static void onDrop(uint8_t side, uint8_t col, uint8_t row) {
    fall.on = true;
    fall.side = side; fall.col = col; fall.row = row;
    fall.t = 0;
    fall.y1 = (int16_t)discY(row);
    int dist = fall.y1 - LANE_Y;
    fall.t1 = (uint8_t)(4 + dist / 9);              // it falls faster the further it has to go
    fall.amp = (uint8_t)(2 + dist / 20);
    hoverX = (int16_t)(discX(col) << 4);
}

static void handle(const game::Event &e) {
    switch (e.type) {
        case game::EV_START:
            shown = game::board;
            live = true;
            break;
        case game::EV_TURN:
            turnSide = e.a;
            humanTurn = e.b != 0;
            tickT = 0;
            if (humanTurn) hoverX = (int16_t)(discX(cur) << 4);
            break;
        case game::EV_DROP:
            onDrop(e.a, e.b, e.c);
            break;
        case game::EV_SAY:
            speak(game::said, e.a, e.b != 0);
            break;
        case game::EV_OVER:
            winnerSide = e.a;
            endFace = e.b;
            fmtStr(endLine, game::said);
            phase = HOLD;
            phaseT = 0;
            break;
    }
}

// ---------------------------------------------------------------------------
// The endings
// ---------------------------------------------------------------------------
static void startScene() {
    phase = SCENE;
    phaseT = 0;
    zoom = zoomTo = 5;
    setCamera(CX, CY);
    fx::clear();
    talkLen = 0;
    skip = false;
    audio::sfx(youWon() ? Sfx::Win : Sfx::Lose);
    if (youWon()) audio::led(audio::LED_PARTY);
}

// No scene (two players, a draw): the last word over the board.
static void startEnd() {
    phase = END;
    phaseT = 0;
    zoomTo = 5;
    skip = false;
    static const char *const WON[2] = {"RED WINS!", "GOLD WINS!"};
    if (overDone) return;                            // (the result is already up: debug)
    if (winnerSide == c4::NOBODY) {
        fx::banner("DRAW", fx::B_WHITE, 74, 100);
        audio::sfx(Sfx::Draw);
    } else {
        fx::banner(WON[winnerSide], winnerSide ? fx::B_GOLD : fx::B_RED, 74, 100);
        audio::sfx(Sfx::Win);
        audio::led(audio::LED_TRIPLE);
    }
    fx::holdBanner(true);
    speak(endLine, endFace, false);
}

static void sceneUpdate() {
    uint16_t t = phaseT;
    if (t < 8) pal::setFade((uint8_t)(t * 2 + 2));
    if (t == 10) {
        fx::banner(youWon() ? "GOOD GAME" : "TRY AGAIN", fx::B_GOLD, 12, 100);
        fx::holdBanner(true);
    }
    // His last word, typed under him from CAPTION_AT.
    if (skip && !overDone) { overDone = true; skip = false; }
    if (t >= 150) overDone = true;
}

static const uint8_t CAPTION_AT = 46;

static void lightNext() {
    uint8_t c = game::four[nLit];
    lit |= c4::cellBit(c);
    static const uint16_t NOTE[4] = {1568, 2093, 2637, 3136};
    audio::blip(NOTE[nLit], 90);
    fx::burst(fx::SPARK, sx(discX(c / 7) + DISC_PX / 2), sy(discY(c % 7) + DISC_PX / 2), 8, 44, WHITE);
    nLit++;
}

static void overUpdate() {
    if (phaseT < 60000) phaseT++;
    switch (phase) {
        case HOLD:
            if (phaseT < (fast ? 8 : 22)) break;
            phaseT = 0;
            if (!fourKnown()) { if (vsCpu() && winnerSide != c4::NOBODY) phase = FADE; else startEnd(); break; }
            if (fast) { phase = LIGHT; break; }
            // In close on the four.
            focusX = focusY = 0;
            for (uint8_t i = 0; i < 4; i++) {
                focusX = (int16_t)(focusX + discX(game::four[i] / 7) + DISC_PX / 2);
                focusY = (int16_t)(focusY + discY(game::four[i] % 7) + DISC_PX / 2);
            }
            focusX /= 4; focusY /= 4;
            zoomTo = 10;
            talkLen = 0;
            audio::sfx(Sfx::Whoosh);
            phase = ZOOM;
            break;
        case ZOOM:
            if (zoom == zoomTo) { phase = LIGHT; phaseT = 0; }
            break;
        case LIGHT:
            if (phaseT % 9 == 1) {
                lightNext();
                if (nLit == 4) { phase = ADMIRE; phaseT = 0; fx::shake(6, 2); }
            }
            break;
        case ADMIRE:
            if (phaseT < (fast ? 30 : 60) && !(skip && phaseT > 10)) break;
            phaseT = 0;
            skip = false;
            if (vsCpu()) phase = FADE;
            else startEnd();
            break;
        case FADE:
            pal::setFade((uint8_t)(16 - phaseT * 2));
            if (phaseT >= 8) startScene();
            break;
        case SCENE:
            sceneUpdate();
            break;
        case END:
            if (winnerSide != c4::NOBODY && !vsCpu() && phaseT % 40 == 10 && phaseT < 130) {
                fx::fountain(30, 126, 12);
                fx::fountain(98, 126, 12);
            }
            if (!overDone && (phaseT >= 80 || (skip && phaseT > 20))) {
                overDone = true;
                fx::holdBanner(false);              // the result panel says it from here
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void update() {
    // A disc on its way down: it drops, knocks, and bounces twice.
    if (fall.on) {
        fall.t++;
        if (fall.t == fall.t1) audio::sfx(Sfx::Drop);
        if (fall.t >= fall.t1 + 12) {
            fall.on = false;
            c4::play(shown, fall.side, fall.col);
        }
    }
    if (denyT) denyT--;

    // The dealer talks: a character a tick, each with a blip of its own.
    if (talkLen) {
        if (talkT < talkLen) {
            char ch = talk[talkT];
            if (ch != ' ' && ch != '\n' && (talkT & 1)) audio::blip((uint16_t)(1500 + (talkT * 97) % 700), 12);
        }
        if (talkT < 0xFFFF) talkT++;
        if (!bubbleUp()) talkLen = 0;
    }
    if (blinkT) blinkT--;
    else if (!--blinkIn) { blinkT = 6; blinkIn = (uint8_t)fx::rndRange(90, 220); }

    game::Event e;
    while (!fall.on && phase == PLAY && game::popEvent(e)) handle(e);

    if (phase != PLAY && !fall.on) overUpdate();

    // The disc waiting to be dropped: yours follows the cursor at once, the
    // CPU's slides between the columns it is weighing, to its clock.
    if (phase == PLAY && !fall.on && live) {
        int target = discX(humanTurn ? cur : game::thinkColumn()) << 4;
        if (humanTurn) hoverX = (int16_t)target;
        else {
            hoverX = (int16_t)(hoverX + (target - hoverX) / 4);
            if (target - hoverX < 4 && hoverX - target < 4) hoverX = (int16_t)target;
            if (++tickT >= 60) tickT = 0;
            if (tickT == 20) audio::sfx(Sfx::Tick);
            if (tickT == 50) audio::sfx(Sfx::Tock);
        }
    }

    // The camera: a step of zoom for every frame drawn, toward the focus.
    if (zoom != zoomTo && zoomDrawn) {
        zoom = (uint8_t)(zoom < zoomTo ? zoom + 1 : zoom - 1);
        zoomDrawn = false;
    }
    if (zoom == 5) setCamera(CX, CY);
    else setCamera(CX + (focusX - CX) * (zoom - 5) / 5, CY + (focusY - CY) * (zoom - 5) / 5);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static int fallY() {
    int t = fall.t;
    if (t < fall.t1) return LANE_Y + (fall.y1 - LANE_Y) * t * t / (fall.t1 * fall.t1);
    int u = t - fall.t1;                             // then a bounce of 8 ticks, and one of 4
    if (u < 8) return fall.y1 - fall.amp * u * (8 - u) / 16;
    u -= 8;
    return fall.y1 - (fall.amp > 3 ? 1 : 0) * u * (4 - u) / 3;
}

static uint8_t expression() {
    uint8_t face = talkFace == E_TALK ? (uint8_t)E_NORMAL : talkFace;
    // The game just ended on the board: he has seen it.
    if (phase == HOLD && winnerSide != c4::NOBODY && vsCpu()) return youWon() ? E_SURPRISED : E_NORMAL;
    if (talking()) return ((talkT >> 2) & 1) ? (uint8_t)E_TALK : face;
    if (bubbleUp()) return face;
    if (blinkT) return E_BLINK;
    return E_NORMAL;
}

static void drawPlaque() {
    panel(PLAQUE_X, PLAQUE_Y, PLAQUE_W, PLAQUE_H, 3, INK, GOLD);
    int x = PLAQUE_X, y = PLAQUE_Y;
    text35(x + PLAQUE_W / 2 - text35Width(plaque.title) / 2, y + 4, plaque.title, GOLD);
    gfx_hline(x + 3, y + 12, PLAQUE_W - 6, NAVY);
    char buf[8];
    for (int i = 0; i < 2; i++) {
        int ty = y + 16 + i * 9;
        text35(x + 5, ty, i ? plaque.labelB : plaque.labelA, i ? SILVER : FELT_LT);
        *fmtInt(buf, i ? plaque.b : plaque.a) = 0;
        text35(x + PLAQUE_W - 5 - text35Width(buf), ty, buf, WHITE);
    }
}

// Whose turn: the disc to be played, and a name under it.
static void drawTurn(uint32_t frame) {
    panel(TURN_X, TURN_Y, TURN_W, TURN_H, 3, INK, GOLD);
    uint8_t s = phase == PLAY ? turnSide : winnerSide;
    if (s > 1) return;
    sprite4(DISC, TURN_X + 6, TURN_Y + 5 - ((phase == PLAY && (frame & 16)) ? 1 : 0), DISC_REMAP[s]);
    const char *who = vsCpu() ? (s == game::YOU ? "YOU" : "CPU") : (s ? "P2" : "P1");
    text35(TURN_X + TURN_W / 2 - text35Width(who) / 2, TURN_Y + 19, who, s ? GOLD : RED);
    if (phase == PLAY) {
        char buf[6];
        *fmtInt(buf, shown.n + 1) = 0;
        text35(TURN_X + TURN_W / 2 - text35Width(buf) / 2, TURN_Y + 26, buf, SILVER);
    }
}

static void drawPlay(uint32_t frame) {
    drawBoard(shown, lit);
    if (zoom != 5) return;
    uint8_t look = talking() ? 1 : (hoverX >> 4) > 58 ? 2 : 1;
    wall();
    dealer(expression(), look);
    if (bubbleUp()) speechBubble(talk, talkT);
    else drawPlaque();
    drawTurn(frame);
    rail(frame);
    // Half the discs each to begin with.
    uint8_t reds = c4::count(shown.side[0]), golds = c4::count(shown.side[1]);
    // ... less those on the board, and the one in hand or on its way down.
    bool hand = fall.on || (phase == PLAY && live);
    uint8_t whose = fall.on ? fall.side : turnSide;
    drawStacks((uint8_t)(21 - reds - (hand && whose == 0)), (uint8_t)(21 - golds - (hand && whose == 1)));
    if (fall.on) {
        int y = fallY();
        drawDisc(discX(fall.col), y, fall.side);
        frameOver(fall.col, y, y + DISC_PX - 1);
    } else if (phase == PLAY && live) {
        int x = hoverX >> 4, y = LANE_Y + ((frame >> 3) & 1);
        if (denyT) x += (denyT & 2) ? 1 : -1;
        drawDisc(x, y, turnSide);
        if (humanTurn && c4::canPlay(shown, cur)) {
            // Where it would come to rest.
            int gx = discX(cur), gy = discY(shown.h[cur]);
            gfx_rect(gx + 3, gy + 3, 4, 4, FX_B);
        }
    }
}

static void drawScene(uint32_t frame) {
    int t = phaseT;
    int typed = t - CAPTION_AT;
    int len = (int)strlen(endLine);
    bool typing = typed >= 0 && typed < len;
    uint8_t expr = blinkT ? (uint8_t)E_BLINK : endFace, look = 1;
    int x = 16, y = 22;
    (void)frame;
    // A quiet room, the light on him, win or lose: he has a word for you.
    gfx_clear(INK);
    dither(0, 0, 128, 128, NAVY, 0);
    gfx_fillEllipse(64, 62, 56, 50, NAVY);
    if (typing && ((t >> 2) & 1)) expr = E_TALK;
    dealer(expr, look, x & ~1, y, true);
    fx::drawParticles(3);
    fx::drawBanner();
    if (!overDone && typed >= 0) {
        panel(10, 95, 108, 31, 3, NAVY, GOLD);
        typedText(64, 111 - (textRows(endLine) * 7) / 2, endLine, typed, WHITE);
    }
}

void renderScene(uint32_t) {
    drawBoard(shown, 0);
}

void renderWall(uint32_t frame, const char *text, uint8_t face) {
    wall();
    bool blink = (frame % 150) < 6;
    dealer(blink ? (uint8_t)E_BLINK : face, 1);
    speechBubble(text, 99);
    rail(frame);
}

// A still scene is not redrawn: the frame is flushed again, so palette
// effects keep moving at 60 Hz, and the bob and the blinks step at 7.5 Hz,
// so an idle board costs an eighth of the frames.
static uint32_t lastSig;

static uint32_t signature(uint32_t frame, uint32_t ui) {
    int lo, hi;
    if (fx::activeRows(lo, hi) || fall.on || zoom != zoomTo || zoom != 5 || phase == SCENE || phase == FADE || talking())
        return frame;
    uint32_t h = 2166136261u;
    uint32_t v[] = {
        cur, (uint32_t)hoverX, turnSide, humanTurn, live, phase, frame >> 3, ui, denyT, shown.n,
        expression(), bubbleUp(), plaque.a, plaque.b, phaseT > 0, (uint32_t)(uintptr_t)plaque.title, overDone,
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
    if (phase == SCENE) { drawScene(frame); return true; }
    drawPlay(frame);
    fx::drawParticles((uint8_t)((2 * zoom + 2) / 5));
    fx::drawBanner();
    fx::applyShake(zoom == 5 ? RAIL_Y + 2 : 0, 127);
    return true;
}

#if CHGAME_DEBUG
void showEnding(uint8_t who) {
    winnerSide = who;
    endFace = E_SMILE;
    fmtStr(endLine, game::said);
    phase = FADE;
    phaseT = 0;
}
#endif

}  // namespace stage
