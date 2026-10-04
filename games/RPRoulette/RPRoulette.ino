// CHRoulette - roulette for the RPGame handheld (CH32X035, 128x128 ST7735,
// piezo), in the casino style of CHBlackjack and CHChess: the croupier from
// CHBlackjack's tables, CHChess's pointing glove on the felt, and a camera
// that whips to the wheel for every spin.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
//
// The files, by role:
//   rules     Roulette.* (the table, the money, a spin's flow), Spots.* (every
//             place a chip can go), Nav.* (the glove's moves), Wheel.* (the
//             pocket orders): no graphics, host-tested
//   the wheel Ball.* (the ball and its solver), WheelArt.* (drawing the wheel)
//   screens   Screens.*: title, play, pause, options, stats, credits, endings
//   show      Presenter.*: the rules' events turned into motion and sound
//   drawing   Table.* (the wall, the croupier), Felt.* (the betting layout),
//             ChipArt.*, Bar.* (the action bar), Remap.* (sprite colours),
//             Layout.h (every coordinate), Fx.* (particles, banners)
//   sound     Sounds.*        saving  Save.*        switches  config.h
//   generated src/assets/ (art, tools/assets.py; WheelMap, tools/wheel.py) and
//             src/audio/Music.* (tools/make_music.py): not edited by hand
#include <RPGame.h>
#include "config.h"
#include "Screens.h"
#include "Save.h"

#if CHGAME_DEBUG
// Game commands for the debug protocol (tools/chsim/chdrive.py 'say').
//   R <seed>          reseed the rules' generator
//   F <n[,n...]>      the next spins' numbers (37 = 00)
//   J <T|P|W|L|O|S|C> jump to a screen
//   G <spot>          put the glove on a spot
//   W <spot> <amount> place a bet there (limits apply)
//   M <amount>        set the purse
//   E <1|0>           let a debug build on the board write its save pages
static bool debugHook(char cmd, const char *args) {
    switch (cmd) {
        case 'R': screens::debugSeed(dbg::parseNum(args, 10)); return true;
        case 'F': {
            const char *p = args;
            while (*p) {
                screens::debugForce((uint8_t)dbg::parseNum(p, 10));
                while (*p == ' ' || *p == ',') p++;
                if (*p && (*p < '0' || *p > '9')) break;
            }
            return true;
        }
        case 'J': screens::debugJump(args[0]); return true;
        case 'G': screens::debugGlove((uint8_t)dbg::parseNum(args, 10)); return true;
        case 'W': {
            const char *p = args;
            uint8_t spot = (uint8_t)dbg::parseNum(p, 10);
            return screens::debugPlace(spot, (uint8_t)dbg::parseNum(p, 10));
        }
        case 'M': screens::debugPurse((int32_t)dbg::parseNum(args, 10)); return true;
        case 'E': save::allowWrites(args[0] == '1'); return true;
    }
    return false;
}
#endif

void setup() {
    rpgame.boot();
    dbg::begin("CHRL " CHRL_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    rpgame.setFrameRate(CHRL_FPS);
#if CHGAME_DEBUG
    dbg::hook = debugHook;
#endif
}

void loop() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the ball
    // and the chips never slow down.
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
