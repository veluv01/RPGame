// CHSlots - the slot machines of the RPGame casino (CH32X035, 128x128
// ST7735, piezo), in the style of CHBlackjack, CHChess and the table games:
// the same palette, lettering, banners and fountains. Three machines share one
// purse: LUCKY 7, a one-armed bandit with three reels; SWEET, three reels of
// candy on five lines with a multiplier for wins in a row; and DRAGON FORTUNE,
// five reels and 25 lines in red and gold with free games, an expanding
// wild, jackpot meters and Hold and Spin.
//
// The 3x5 font comes from Press Play On Tape's Arduboy Blackjack
// (Apache-2.0) by Simon Holmes (filmote) and Stephane C (vampirics), by way
// of CHBlackjack; see NOTICE.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
//
// The files, by role:
//   rules     Slots.*: the three machines, settled spin by spin (no graphics)
//   screens   Screens.*: title, machine menu, play, options, stats, endings
//             (and the debug commands)
//   show      Presenter.*: replays a settled spin as reels, wins and banners
//   drawing   Machine.* (cabinets, reels, wheel, paytable, felts), Layout.h
//             (every coordinate), Fx.* (particles, banners)
//   sound     Sounds.* (effects and three tunes)   saving  Save.*
//   switches  config.h
//   generated src/assets/ (art, tools/assets.py), src/game/Strips.h (the reel
//             strips, tools/strips.py): not edited by hand
#include <RPGame.h>
#include "config.h"
#include "Machine.h"
#include "Screens.h"

void setup() {
    rpgame.boot();
    dbg::begin("CHSL " CHSL_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    pal::setThemes(mach::THEMES, mach::THEME_COUNT);    // each machine's felt
    screens::begin();
    rpgame.setFrameRate(CHSL_FPS);
#if CHGAME_DEBUG
    dbg::hook = screens::debugCommand;
#endif
}

void loop() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the reels
    // never slow down.
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
