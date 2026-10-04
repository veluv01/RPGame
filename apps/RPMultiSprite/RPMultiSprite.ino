/*
 * RPMultiSprite - how many animated characters can walk around the screen at
 * once, with every frame streamed from microSD? A stress test for the RPGame
 * board (RP2040/RP2350, ST7735 128x128, SD and LCD sharing hardware SPI).
 *
 * Built on RPSpriteView (the optimised SD sprite streamer). Two characters,
 * Moog and Mushboom, seen 3/4 from above, each with a 3-frame walk cycle per
 * direction (frames 0-2 up, 3-5 right, 6-8 down, 9-11 left). Each walker
 * wanders the screen on a random path and plays the loop for the way it faces.
 *
 * ---------------------------------------------------------------------------
 * SD CARD
 * ---------------------------------------------------------------------------
 *   /WALK.BIN   (sample/WALK.BIN in this folder: 24 frames, one per block)
 *   Rebuild it, and src/WalkerData.h, with tools/build_walkers.py.
 *
 * ---------------------------------------------------------------------------
 * DRAW MODES (SELECT cycles) - the point of the test
 * ---------------------------------------------------------------------------
 *   SD-EACH   spriteDraw() per walker, back to front. One card command each:
 *             what you get by using the RPSpriteView API naively.
 *   SD-BATCH  SpriteBatch with depth layers. Walkers that do not overlap
 *             share card commands; overlap order is still correct. This is
 *             the one to use in a game.
 *   SD-SHEET  SpriteBatch ignoring depth: ONE command streams every frame
 *             in use. Fastest possible, but overlapping walkers are drawn
 *             in frame order, not depth order (watch for it).
 *   FLASH     the same frames from flash (src/WalkerData.h): the cost with
 *             no SD at all, as a baseline.
 *
 * ---------------------------------------------------------------------------
 * CONTROLS
 * ---------------------------------------------------------------------------
 *   A / B        +1 / -1 walker (hold to repeat)
 *   UP / DOWN    +8 / -8 walkers
 *   SELECT       next draw mode
 *   START        automatic sweep: every mode, 2, 4, 6... walkers, 1 s each,
 *                CSV on Serial, then a summary of the most walkers each mode
 *                holds at 60 and 30 fps. START again aborts.
 *
 * HUD: mode, walkers, depth layers (L) and card commands (R) per frame, fps,
 * draw ms (clear + all sprites into the framebuffer, SD time included) and
 * lcd ms (panel transfer). Serial prints the same once a second.
 *
 * Build with Tools > Optimize > "Faster (-O2)", as RPSpriteView.
 */
#include <RPGame.h>
#include "src/RPSpriteView.h"
#include "src/SpriteBatch.h"
#include "src/WalkerData.h"

/* ------------------------------------------------------------------------ */
/* Tuning knobs                                                              */
/* ------------------------------------------------------------------------ */
#ifndef DEMO_START_WALKERS
#define DEMO_START_WALKERS 16
#endif

#ifndef DEMO_START_MODE
#define DEMO_START_MODE MODE_SD_BATCH
#endif

/* 1 = run the sweep straight after boot (for bench.py, no button needed). */
#ifndef SWEEP_ON_BOOT
#define SWEEP_ON_BOOT 0
#endif

#define MAX_WALKERS        SPRITE_BATCH_MAX
#define WALK_TICK_MS       40      /* one pixel per tick: 25 px/s             */
#define WALK_MAX_CATCHUP   8       /* ticks per loop at most (slow frames)    */
#define WALK_PX_PER_FRAME  4       /* advance the walk cycle every 4 px       */
#define WALK_MIN_STEPS     8       /* each leg of the random path, in px      */
#define WALK_MAX_STEPS     48

#define SWEEP_STEP         2
#define SWEEP_SETTLE_MS    250
#define SWEEP_MEASURE_MS   1000
#define SWEEP_MIN_FPS      20      /* stop a mode once it falls below this    */

/* Screen layout: two HUD text lines on top, walkers below. */
#define HUD_H    18
#define PLAY_Y0  HUD_H
#define PLAY_H   (GFX_H - PLAY_Y0)

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */
enum Mode : uint8_t { MODE_SD_EACH, MODE_SD_BATCH, MODE_SD_SHEET, MODE_FLASH, MODE_COUNT };
static const char *const modeName[MODE_COUNT] = { "SD-EACH", "SD-BATCH", "SD-SHEET", "FLASH" };

enum Dir : uint8_t { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };
static const int8_t dirDx[4] = { 0, 1, 0, -1 };
static const int8_t dirDy[4] = { -1, 0, 1, 0 };

struct Walker {
    int16_t x, y;          /* top-left; drawn at even x (fast blit path)      */
    int16_t drawnX, drawnY;/* where it was last drawn, to erase it            */
    uint8_t type;          /* 0 Moog, 1 Mushboom                              */
    uint8_t dir;
    uint8_t steps;         /* pixels left on this leg of the path             */
    uint8_t walked;        /* pixels walked, drives the walk cycle            */
};

/* One measurement window, averaged per frame. */
struct Report { uint32_t fps, drawUs, lcdUs, layers10, reads10, blocks; };

RPGame rpGame;

static SpriteFile  frames[WALK_FRAMES];   /* where each frame lives on the card */
static SpriteBatch batch;
static Walker      walkers[MAX_WALKERS];
static uint8_t     order[MAX_WALKERS];    /* back to front (by feet y)          */
static uint8_t     walkerCount = 0;
static uint8_t     mode        = DEMO_START_MODE;
static bool        fullFlush   = true;    /* next frame repaints the play area  */
static uint32_t    nextTick;

static uint8_t colText, colDim, colAccent, colHudBg;

/* Per-window accumulators. */
static uint32_t statStart, statFrames, statDrawUs, statLcdUs;
static uint32_t statLayers, statReads, statBlocks, statErrors;

/* Sweep. */
static bool     sweeping = false;
static bool     sweepMeasuring;
static uint8_t  sweepMode;
static uint32_t sweepPhaseStart;
static uint8_t  best60[MODE_COUNT], best30[MODE_COUNT];

/* ------------------------------------------------------------------------ */
/* Random numbers: xorshift32, reseeded so every sweep step is repeatable    */
/* ------------------------------------------------------------------------ */
static uint32_t rngState = 1;
static uint32_t rnd(void)
{
    uint32_t x = rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rngState = x;
}
static uint16_t rndRange(uint16_t lo, uint16_t hi) { return lo + rnd() % (hi - lo + 1); }

/* ------------------------------------------------------------------------ */
/* Tiny formatting helpers (no printf: keeps flash and RAM down)             */
/* ------------------------------------------------------------------------ */
static char *putU32(char *p, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

/* microseconds -> "m.d" milliseconds */
static char *putMs(char *p, uint32_t us)
{
    p = putU32(p, us / 1000);
    *p++ = '.';
    *p++ = (char)('0' + (us % 1000) / 100);
    *p = 0;
    return p;
}

static char *putStr(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

/* ------------------------------------------------------------------------ */
/* Walkers                                                                   */
/* ------------------------------------------------------------------------ */
static inline uint8_t wW(const Walker &w) { return walkW[w.type]; }
static inline uint8_t wH(const Walker &w) { return walkH[w.type]; }
static inline int16_t drawX(const Walker &w) { return (int16_t)(w.x & ~1); }

static bool inside(const Walker &w, int x, int y)
{
    return x >= 0 && x + wW(w) <= GFX_W && y >= PLAY_Y0 && y + wH(w) <= GFX_H;
}

/* New leg of the path: a random direction that can take at least one step. */
static void pickLeg(Walker &w)
{
    uint8_t d = (uint8_t)(rnd() & 3);
    for (uint8_t k = 0; k < 4; k++, d = (uint8_t)((d + 1) & 3)) {
        if (inside(w, w.x + dirDx[d], w.y + dirDy[d])) break;
    }
    w.dir   = d;
    w.steps = (uint8_t)rndRange(WALK_MIN_STEPS, WALK_MAX_STEPS);
}

static void stepWalker(Walker &w)
{
    if (w.steps == 0) pickLeg(w);
    const int nx = w.x + dirDx[w.dir], ny = w.y + dirDy[w.dir];
    if (!inside(w, nx, ny)) {       /* hit an edge: turn, walk on next tick */
        pickLeg(w);
        return;
    }
    w.x = (int16_t)nx;
    w.y = (int16_t)ny;
    w.steps--;
    w.walked++;
}

static inline uint8_t walkerFrame(const Walker &w)
{
    return (uint8_t)(w.type * WALK_FRAMES_PER_TYPE + w.dir * 3 +
                     (w.walked / WALK_PX_PER_FRAME) % 3);
}

static void spawnWalker(uint8_t i)
{
    Walker &w = walkers[i];
    w.type   = i & 1;               /* alternate Moog / Mushboom */
    w.x      = (int16_t)rndRange(0, GFX_W - wW(w));
    w.y      = (int16_t)rndRange(PLAY_Y0, GFX_H - wH(w));
    w.drawnX = w.drawnY = -1;
    w.walked = (uint8_t)rnd();
    w.steps  = 0;
    pickLeg(w);
}

static void setWalkerCount(int n)
{
    if (n < 0) n = 0;
    if (n > MAX_WALKERS) n = MAX_WALKERS;
    while (walkerCount < n) spawnWalker(walkerCount++);
    walkerCount = (uint8_t)n;
    for (uint8_t i = 0; i < walkerCount; i++) order[i] = i;
    fullFlush = true;
}

/* Same seed, same crowd: makes every sweep step comparable across modes. */
static void resetWalkers(uint8_t n)
{
    rngState = 0x1234567u;
    walkerCount = 0;
    setWalkerCount(n);
    nextTick = millis();
}

/* Back to front by feet: insertion sort, the order barely changes per frame. */
static void sortByDepth(void)
{
    for (uint8_t i = 1; i < walkerCount; i++) {
        const uint8_t v = order[i];
        const int16_t k = walkers[v].y + wH(walkers[v]);
        uint8_t j = i;
        while (j && walkers[order[j - 1]].y + wH(walkers[order[j - 1]]) > k) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
}

/* ------------------------------------------------------------------------ */
/* Screen furniture                                                          */
/* ------------------------------------------------------------------------ */
static void drawHud(uint32_t fps, uint32_t drawUs, uint32_t lcdUs,
                    uint32_t layers10, uint32_t reads10)
{
    char line[24], *p;
    gfx_wait();                          /* the HUD shares the bus */
    gfx_fillRect(0, 0, GFX_W, HUD_H, colHudBg);

    p = putStr(line, modeName[mode]);
    gfx_text(1, 1, line, sweeping ? colText : colAccent);
    p = putStr(line, "n");  p = putU32(p, walkerCount);
    if (mode == MODE_SD_BATCH || mode == MODE_SD_SHEET) {
        p = putStr(p, " L"); p = putU32(p, (layers10 + 5) / 10);
    }
    if (mode != MODE_FLASH) {
        p = putStr(p, " R"); p = putU32(p, (reads10 + 5) / 10);
    }
    gfx_text(GFX_W - 1 - 6 * (int)(p - line), 1, line, colText);

    p = putU32(line, fps); p = putStr(p, "fps d");
    p = putMs(p, drawUs);  p = putStr(p, " l");
    p = putMs(p, lcdUs);
    if (statErrors) { p = putStr(p, " E"); putU32(p, statErrors); }
    gfx_text(1, 10, line, sweeping ? colAccent : colDim);

    gfx_flushRect(0, 0, GFX_W, HUD_H);
}

static void fatal(const char *l1, const char *l2)
{
    gfx_clear(colHudBg);
    gfx_text(4, 50, l1, colAccent);
    if (l2) gfx_text(4, 62, l2, colText);
    gfx_text(4, 80, "START = retry", colDim);
    gfx_flush();
}

/* ------------------------------------------------------------------------ */
/* Card + frame loading                                                      */
/* ------------------------------------------------------------------------ */
static const char *const sdTypeName[] = { "?", "SD1", "SD2", "SDHC" };

static bool loadAll(void)
{
    if (!SD.begin(PIN_SD_CS)) {
        fatal("SD mount failed", "card? FAT16/32?");
        if (Serial) Serial.println(F("SD mount failed"));
        return false;
    }
    Sd2Card &card = SD.rawCard();
    if (Serial) {
        Serial.print(F("SD: "));
        Serial.print(sdTypeName[card.type() & 3]);
        Serial.print(F("  SPI "));
        Serial.print(card.sckHz() / 1000000u);
        Serial.print(F(" MHz  DMA "));
        Serial.println(Sd2Card::dmaEnabled() ? F("on") : F("off"));
    }

    /* All 24 frames from one file, resolved to block addresses once. */
    int r = spriteLoadSheet(frames, WALK_FRAMES, "WALK.BIN", 1);
    if (Serial) {
        Serial.print(F("WALK.BIN  "));
        if (r == SPRITE_OK) {
            Serial.print(F("lba "));      Serial.print(frames[0].firstBlock);
            Serial.print(F("  frames ")); Serial.print(WALK_FRAMES);
            Serial.print(F("  "));        Serial.print(frames[0].w);
            Serial.print('x');            Serial.println(frames[0].h);
        } else {
            Serial.print(F("ERROR "));    Serial.println(r);
        }
    }
    if (r == SPRITE_ERR_FRAGMENTED) { fatal("WALK.BIN fragmented", "recopy to fresh card"); return false; }
    if (r != SPRITE_OK)             { fatal("No /WALK.BIN", "see sample/ folder");          return false; }

    /* The flash frames must match the card, or the modes compare apples to
     * pears: check the sizes of both character types. */
    for (uint8_t t = 0; t < WALK_TYPES; t++) {
        const SpriteFile &s = frames[t * WALK_FRAMES_PER_TYPE];
        if (s.w != walkW[t] || s.h != walkH[t]) {
            fatal("WALK.BIN mismatch", "rebuild + recopy");
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* One frame                                                                 */
/* ------------------------------------------------------------------------ */
static void renderFrame(void)
{
    /* ---- 1. game logic: runs while the previous flush is on the wire -- */
    uint32_t now = millis();
    uint8_t ticks = 0;
    while ((int32_t)(now - nextTick) >= 0 && ticks < WALK_MAX_CATCHUP) {
        for (uint8_t i = 0; i < walkerCount; i++) stepWalker(walkers[i]);
        nextTick += WALK_TICK_MS;
        ticks++;
    }
    if ((int32_t)(now - nextTick) >= 0) nextTick = now;   /* too far behind: drop */
    sortByDepth();

    /* ---- 2. what the panel must be told about: old and new positions -- */
    SpriteDirty d;
    spriteDirtyReset(d);
    if (fullFlush) {
        spriteDirtyAddRect(d, 0, PLAY_Y0, GFX_W, PLAY_H);
        fullFlush = false;
    } else {
        for (uint8_t i = 0; i < walkerCount; i++) {
            const Walker &w = walkers[i];
            if (w.drawnX >= 0) spriteDirtyAddRect(d, w.drawnX, w.drawnY, wW(w), wH(w));
            spriteDirtyAddRect(d, drawX(w), w.y, wW(w), wH(w));
        }
    }

    /* ---- 3. wait for the previous panel transfer ---------------------- */
    uint32_t t0 = micros();
    gfx_wait();
    uint32_t t1 = micros();

    /* ---- 4. clear the play area and draw every walker, back to front -- */
    memset(gfx_fb + PLAY_Y0 * GFX_FB_STRIDE, 0, PLAY_H * GFX_FB_STRIDE);

    uint16_t reads = 0, blocks = 0;
    uint8_t  layers = 0;
    switch (mode) {
    case MODE_SD_EACH:
        for (uint8_t k = 0; k < walkerCount; k++) {
            const Walker &w = walkers[order[k]];
            if (spriteDraw(frames[walkerFrame(w)], drawX(w), w.y, 0) != SPRITE_OK) statErrors++;
        }
        reads = blocks = walkerCount;
        break;

    case MODE_SD_BATCH:
    case MODE_SD_SHEET:
        spriteBatchBegin(batch);
        for (uint8_t k = 0; k < walkerCount; k++) {
            const Walker &w = walkers[order[k]];
            spriteBatchAdd(batch, frames[walkerFrame(w)], drawX(w), w.y, 0);
        }
        if (spriteBatchDraw(batch, mode == MODE_SD_SHEET ? SPRITE_BATCH_IGNORE_DEPTH : 0)
                != SPRITE_OK) statErrors++;
        layers = batch.stats.layers;
        reads  = batch.stats.reads;
        blocks = batch.stats.blocks;
        break;

    default: /* MODE_FLASH */
        for (uint8_t k = 0; k < walkerCount; k++) {
            const Walker &w = walkers[order[k]];
            gfx_blit(walkFlashFrames[walkerFrame(w)], drawX(w), w.y, wW(w), wH(w), 0);
        }
        break;
    }
    uint32_t t2 = micros();

    for (uint8_t i = 0; i < walkerCount; i++) {
        walkers[i].drawnX = drawX(walkers[i]);
        walkers[i].drawnY = walkers[i].y;
    }

    /* ---- 5. framebuffer -> panel, asynchronously ----------------------- */
    spriteFlushDirty(d);
    uint32_t t3 = micros();

    statDrawUs += t2 - t1;
    statLcdUs  += (t1 - t0) + (t3 - t2);
    statLayers += layers;
    statReads  += reads;
    statBlocks += blocks;
    statFrames++;
}

/* ------------------------------------------------------------------------ */
/* Statistics                                                                */
/* ------------------------------------------------------------------------ */
static Report takeReport(uint32_t now)
{
    Report r;
    const uint32_t f  = statFrames ? statFrames : 1;
    const uint32_t ms = (now - statStart) ? (now - statStart) : 1;
    r.fps      = (statFrames * 1000u + ms / 2) / ms;
    r.drawUs   = statDrawUs / f;
    r.lcdUs    = statLcdUs / f;
    r.layers10 = statLayers * 10u / f;
    r.reads10  = statReads * 10u / f;
    r.blocks   = statBlocks / f;
    return r;
}

static void resetStats(void)
{
    statStart  = millis();
    statFrames = statDrawUs = statLcdUs = 0;
    statLayers = statReads = statBlocks = 0;
}

static void printTenths(uint32_t t)
{
    Serial.print(t / 10); Serial.print('.'); Serial.print(t % 10);
}

/* ------------------------------------------------------------------------ */
/* Sweep                                                                     */
/* ------------------------------------------------------------------------ */
static void sweepBeginStep(uint8_t n)
{
    resetWalkers(n);
    sweepMeasuring  = false;
    sweepPhaseStart = millis();
}

static void sweepStart(void)
{
    sweeping  = true;
    sweepMode = 0;
    for (uint8_t m = 0; m < MODE_COUNT; m++) best60[m] = best30[m] = 0;
    mode = sweepMode;
    if (Serial) Serial.println(F("\nmode,walkers,fps,draw_us,lcd_us,layers,reads,blocks,err"));
    sweepBeginStep(SWEEP_STEP);
}

static void showSummary(void)
{
    gfx_wait();
    gfx_clear(colHudBg);
    gfx_text(4, 4, "MOST WALKERS AT", colAccent);
    gfx_text(4, 16, "mode      @60  @30", colDim);
    char line[24], *p;
    for (uint8_t m = 0; m < MODE_COUNT; m++) {
        p = putStr(line, modeName[m]);
        while (p < line + 10) *p++ = ' ';
        p = putU32(p, best60[m]);
        while (p < line + 15) *p++ = ' ';
        p = putU32(p, best30[m]);
        gfx_text(4, 28 + 10 * m, line, colText);
    }
    p = putStr(line, "(");  p = putU32(p, MAX_WALKERS);  putStr(p, " = the cap)");
    gfx_text(4, 80, line, colDim);
    gfx_text(4, 110, "any button: continue", colDim);
    gfx_flush();

    if (Serial) {
        Serial.println(F("\nmost walkers held   @60fps  @30fps"));
        for (uint8_t m = 0; m < MODE_COUNT; m++) {
            Serial.print(F("  "));  Serial.print(modeName[m]);
            Serial.print(F("\t"));  Serial.print(best60[m]);
            Serial.print(F("\t"));  Serial.println(best30[m]);
        }
    }

    do { rpGame.pollButtons(); delay(10); } while (rpGame.buttonsState() == 0);
    do { rpGame.pollButtons(); delay(10); } while (rpGame.buttonsState() != 0);
}

static void sweepUpdate(uint32_t now)
{
    if (!sweepMeasuring) {
        if (now - sweepPhaseStart >= SWEEP_SETTLE_MS) {
            resetStats();
            sweepMeasuring = true;
        }
        return;
    }
    if (now - statStart < SWEEP_MEASURE_MS) return;

    Report r = takeReport(now);
    if (Serial) {
        Serial.print(modeName[mode]);   Serial.print(',');
        Serial.print(walkerCount);      Serial.print(',');
        Serial.print(r.fps);            Serial.print(',');
        Serial.print(r.drawUs);         Serial.print(',');
        Serial.print(r.lcdUs);          Serial.print(',');
        printTenths(r.layers10);        Serial.print(',');
        printTenths(r.reads10);         Serial.print(',');
        Serial.print(r.blocks);         Serial.print(',');
        Serial.println(statErrors);
    }
    drawHud(r.fps, r.drawUs, r.lcdUs, r.layers10, r.reads10);

    if (r.fps >= 60) best60[mode] = walkerCount;
    if (r.fps >= 30) best30[mode] = walkerCount;

    if (r.fps >= SWEEP_MIN_FPS && walkerCount + SWEEP_STEP <= MAX_WALKERS) {
        sweepBeginStep((uint8_t)(walkerCount + SWEEP_STEP));
        return;
    }
    if (++sweepMode < MODE_COUNT) {
        mode = sweepMode;
        sweepBeginStep(SWEEP_STEP);
        return;
    }

    /* Done: show the table, then carry on interactively. */
    sweeping = false;
    showSummary();
    mode = DEMO_START_MODE;
    resetWalkers(DEMO_START_WALKERS);
    resetStats();
}

/* ------------------------------------------------------------------------ */
/* Input                                                                     */
/* ------------------------------------------------------------------------ */
static void handleButtons(uint32_t now)
{
    /* Uncapped, the loop runs every few ms - faster than a switch stops
     * bouncing - so one action per 180 ms, which doubles as hold-to-repeat. */
    static uint32_t nextAct;
    rpGame.pollButtons();
    if ((int32_t)(now - nextAct) < 0) return;

    if (rpGame.pressed(START_BUTTON)) {
        nextAct = now + 300;             /* a slower toggle */
        if (sweeping) {
            sweeping = false;
            if (Serial) Serial.println(F("sweep aborted"));
            resetStats();
        } else {
            sweepStart();
        }
        return;
    }
    if (sweeping) return;                /* the sweep owns count and mode */

    int n = walkerCount;
    if      (rpGame.pressed(A_BUTTON))    n += 1;
    else if (rpGame.pressed(B_BUTTON))    n -= 1;
    else if (rpGame.pressed(UP_BUTTON))   n += 8;
    else if (rpGame.pressed(DOWN_BUTTON)) n -= 8;
    else if (rpGame.pressed(SELECT_BUTTON)) {
        mode = (uint8_t)((mode + 1) % MODE_COUNT);
        fullFlush = true;
        resetStats();
        nextAct = now + 300;
        return;
    } else {
        return;
    }
    if (n != walkerCount) {
        setWalkerCount(n);
        resetStats();
    }
    nextAct = now + 180;
}

/* ------------------------------------------------------------------------ */
/* setup / loop                                                              */
/* ------------------------------------------------------------------------ */
void setup()
{
    /* 24 MHz SPI, 12 bpp: 25% less panel traffic than 16 bpp. */
    Gfx.begin(GFX_DIV2, GFX_12BPP);
    Gfx.setPalette(walkerPalette, 16);
    colText   = gfx_nearest(0xFFFF);
    colDim    = gfx_nearest(0xC618);
    colAccent = gfx_nearest(0xFE00);
    colHudBg  = gfx_nearest(0x0000);

    rpGame.boot();

    gfx_clear(colHudBg);
    gfx_text(4, 60, "mounting SD...", colText);
    gfx_flush();

    /* Give a host a moment to open the serial port for the boot report. */
    { const uint32_t t = millis(); while (!Serial && uint32_t(millis() - t) < 1500u) delay(1); }

    while (!loadAll()) {
        do { rpGame.pollButtons(); delay(10); } while (!rpGame.justPressed(START_BUTTON));
    }

    gfx_clear(0);
    gfx_flush();
    resetWalkers(DEMO_START_WALKERS);
    drawHud(0, 0, 0, 0, 0);
    resetStats();
#if SWEEP_ON_BOOT
    sweepStart();
#endif
}

void loop()
{
    uint32_t now = millis();
    handleButtons(now);

    renderFrame();

    now = millis();
    if (sweeping) {
        sweepUpdate(now);
        return;
    }

    /* ---- once a second: report ---------------------------------------- */
    if (now - statStart >= 1000) {
        Report r = takeReport(now);
        drawHud(r.fps, r.drawUs, r.lcdUs, r.layers10, r.reads10);
        if (Serial) {
            Serial.print(modeName[mode]);
            Serial.print(F("  walkers "));  Serial.print(walkerCount);
            Serial.print(F("  fps "));      Serial.print(r.fps);
            Serial.print(F("  draw "));     Serial.print(r.drawUs);
            Serial.print(F("us  lcd "));    Serial.print(r.lcdUs);
            Serial.print(F("us  layers ")); printTenths(r.layers10);
            Serial.print(F("  reads "));    printTenths(r.reads10);
            Serial.print(F("  blocks "));   Serial.print(r.blocks);
            Serial.print(F("  err "));      Serial.println(statErrors);
        }
        resetStats();
    }
}
