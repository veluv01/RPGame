// The frame the .ino's loop() runs (Frame.h): logic ticks at 60 Hz while
// the last frame is still going out over DMA, then drawing and the flush.
#pragma GCC optimize("Os", "no-ipa-sra")
#include <Arduino.h>
#include <RPGame.h>
#include "config.h"
#include "Frame.h"
#include "Screens.h"
#include "Sounds.h"

namespace frame {

void begin() {
    dbg::begin("CHCW " CHCW_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);
    pal::init();
    screens::begin();
}

// Logic runs while the previous frame is still going out over DMA; drawing
// waits for it (one framebuffer), then the new frame is sent.
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
