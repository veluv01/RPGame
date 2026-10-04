// CHCraps - casino craps for the RPGame handheld (CH32X035, 128x128 ST7735,
// piezo), in the style of CHBlackjack and CHChess: the same felt, the same
// lettering, and Blackjack's dealer working the table as the stickman.
//
// The dealer's art and the 3x5 font come from Press Play On Tape's Arduboy
// Blackjack (Apache-2.0) by Simon Holmes (filmote) and Stephane C
// (vampirics), by way of CHBlackjack; see NOTICE.
//
// The files, by role:
//   rules     Craps (bets, money, the point, the dice; no graphics)
//   screens   Screens (title, play and its pause menu, options, stats, the ends)
//   the show  Presenter (chips flying, the dealer paying, the stickman's calls),
//             Cam and Dice3D (the dice cam: the throw in 3D)
//   drawing   Layout (every coordinate), Zones (every spot on the layout, as
//             data), Felt, Wall, Bar, Chips; Fx (particles, banners, floats);
//             the art is generated, src/assets/
//   sound     Sounds
//   saving    Save
//   config.h  the build switches
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
#include <RPGame.h>
#include "config.h"
#include "Screens.h"

void setup() {
    rpgame.boot();
    dbg::begin("CHCR " CHCR_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    rpgame.setFrameRate(CHCR_FPS);
#if CHGAME_DEBUG
    dbg::hook = screens::debugCommand;
#endif
}

void loop() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the dice
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
