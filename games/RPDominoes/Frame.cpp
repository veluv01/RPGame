// The frame loop the .ino calls: start-up, then each frame the logic
// ticks, the drawing and the DMA flush.
#pragma GCC optimize("Os", "no-ipa-sra")
#include <Arduino.h>
#include <RPGame.h>
#include "config.h"
#include "Frame.h"
#include "Colours.h"
#include "Screens.h"
#include "Sounds.h"

namespace frame {

void begin() {
    dbg::begin("CHDM " CHDM_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);   // on once the options are read
    pal::init(COLOURS);
    screens::begin();
}

// Logic runs while the previous frame is still going out over DMA; drawing
// waits for it (one framebuffer), then the new frame is sent. The CPU's
// thinking is a slice of each logic tick (Ai.h), so there is only
// this one loop.
bool run() {
    dbg::poll();
    if (!rpgame.nextFrame()) return false;
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
    return true;
}

}  // namespace frame
