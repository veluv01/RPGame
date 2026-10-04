// CHBlackjack - Press Play On Tape's Arduboy Blackjack, rebuilt in colour for
// the RPGame handheld (CH32X035, 128x128 ST7735, piezo).
//
// Derived from Press-Play-On-Tape/Blackjack (Apache-2.0) by Simon Holmes
// (filmote) and Stephane C (vampirics).
// Modified 2026 for RPGame by bateske: see NOTICE for what changed.
//
// The files, by role:
//   rules      Round (the table, money and the flow of a round)
//   screens    Screens (splash, title, play, win/lose, options, stats, credits)
//   motion     Presenter (cards and chips in flight), Fx (sparkle)
//   drawing    Layout (every coordinate), Table, CardArt, Bar (the buttons)
//   sound      Sounds; the music is generated (src/audio, tools/make_music.py)
//   saving     Save
//   generated  src/assets (tools/assets.py: PPOT's art, recoloured)
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
#include <RPGame.h>
#include "config.h"
#include "Screens.h"

#if CHGAME_DEBUG
// Game commands for the debug protocol (tools/chsim/chdrive.py 'say').
//   R <seed>          reseed the shoe
//   D <c1,c2,...>     stack the next cards dealt (0..51)
//   J <T|P|W|L|O|S|C> jump to a screen (C = credits)
static bool debugHook(char cmd, const char *args) {
    switch (cmd) {
        case 'R': screens::debugSeed(dbg::parseNum(args, 10)); return true;
        case 'D': {
            uint8_t cards[16], n = 0;
            const char *p = args;
            while (*p && n < 16) cards[n++] = (uint8_t)dbg::parseNum(p, 10);
            screens::debugStack(cards, n);
            return true;
        }
        case 'J': screens::debugJump(args[0]); return true;
    }
    return false;
}
#endif

void setup() {
    rpgame.boot();
    dbg::begin("CHBJ " CHBJ_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    rpgame.setFrameRate(CHBJ_FPS);
#if CHGAME_DEBUG
    dbg::hook = debugHook;
#endif
}

void loop() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so dealing
    // and animations never slow down.
    uint8_t ticks = 0;
    do {
        rpgame.pollButtons();
        pal::tick();
        screens::update();
    } while (++ticks < 3 && rpgame.nextFrame());
    pal::commit();                  // staged by RPGfx: lands with the next flush
    gfx_wait();
    dbg::markRenderStart();
    screens::render(rpgame.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
}
