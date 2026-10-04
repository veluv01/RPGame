// One frame (Frame.h), run by CHBoardwalk.ino's loop(): the debug
// protocol, logic at a fixed 60 Hz, then drawing and the DMA flush.
#pragma GCC optimize("Os")
#include <Arduino.h>
#include <RPGame.h>
#include "config.h"
#include "Frame.h"
#include "Screens.h"
#include "Sounds.h"

namespace frame {

void begin() {
    dbg::begin("CHBW " CHBW_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);   // on once the options are read
    pal::init();
    screens::begin();
}

void run() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again.
    uint8_t ticks = 0;
    do {
        rpgame.pollButtons();
        pal::tick();
        audio::update();
        screens::update();
    } while (++ticks < 3 && rpgame.nextFrame());
    gfx_wait();
    pal::commit();
    dbg::markRenderStart();
    screens::render(rpgame.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
}

}  // namespace frame
