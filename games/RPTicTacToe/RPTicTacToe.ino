// CHTicTacToe - TIC TAC TOE: ROYALE for the RPGame handheld (CH32X035,
// 128x128 ST7735, piezo), in the casino style of CHBlackjack and CHChess:
// noughts and crosses for money, at sixteen tables with sixteen sets of rules,
// against CHBlackjack's croupier.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
//
// The files, by role:
//   rules, no graphics (host-tested)  Rules (every table), Match, Cpu (the dealer), Text
//   the play screen                   Stage, Iso (the 3D tables), ChipArt, Remap, Fx
//   the screens                       Screens, Table (the dealer's wall); debug commands below
//   sound, saving                     Sounds, Save
//   generated art (tools/assets.py)   src/assets/Assets.*
//   build switches                    config.h
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Save.h"

#if CHGAME_DEBUG
// Game commands for the debug protocol (tools/chsim/chdrive.py 'say').
//   R <seed>          reseed the match's generator
//   J <T|G|P|W|L|O|S> [table]  jump to a screen (G the tables room, P play)
//   C <cell> [arg]    play that cell for the player (arg: size or symbol)
//   H <cell>          the dealer's next move
//   M <amount>        set the purse
//   D <0..2>          the dealer: tipsy, sharp, shark
//   V <ticks>         BLITZ: set the clock
//   E <1|0>           let a debug build on the board write its save pages
static bool debugHook(char cmd, const char *args) {
    switch (cmd) {
        case 'R': screens::debugSeed(dbg::parseNum(args, 10)); return true;
        case 'J': {
            const char *p = args + 1;
            screens::debugJump(args[0], (uint8_t)dbg::parseNum(p, 10));
            return true;
        }
        case 'C': {
            const char *p = args;
            uint8_t cell = (uint8_t)dbg::parseNum(p, 10);
            screens::debugCell(cell, (uint8_t)dbg::parseNum(p, 10));     // not legal: nothing happens
            return true;
        }
        case 'H': screens::debugDealer((uint8_t)dbg::parseNum(args, 10)); return true;
        case 'M': screens::debugPurse((int32_t)dbg::parseNum(args, 10)); return true;
        case 'D': screens::debugLevel((uint8_t)dbg::parseNum(args, 10)); return true;
        case 'V': screens::debugClock((uint16_t)dbg::parseNum(args, 10)); return true;
        case 'E': save::allowWrites(args[0] == '1'); return true;
    }
    return false;
}
#endif

void setup() {
    rpgame.boot();
    dbg::begin("CHTT " CHTT_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    rpgame.setFrameRate(CHTT_FPS);
#if CHGAME_DEBUG
    dbg::hook = debugHook;
#endif
}

void loop() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the clock
    // and the gloves never slow down.
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
