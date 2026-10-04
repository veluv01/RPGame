#pragma GCC optimize("Os")
// One frame of the game (Frame.h), and begin(): the felt colours, sound, the screens.
#include <Arduino.h>
#include <RPGame.h>
#include "config.h"
#include "Frame.h"
#include "Screens.h"
#include "Sounds.h"

namespace frame {

// The table's colours: FELT_DK and FELT only. FELT_LT stays green whatever
// the table - it is the bamboo suit's ink.
static const uint16_t FELTS[pal::THEME_COUNT][3] = {
    {0x042, 0x173, 0x4B5},   // classic green
    {0x024, 0x149, 0x4B5},   // blue
    {0x401, 0x812, 0x4B5},   // red
    {0x203, 0x517, 0x4B5},   // purple
};

void begin() {
    dbg::begin("CHMJ " CHMJ_VERSION);   // the debug protocol's hello (CHGAME_DEBUG builds)
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);   // on once the options are read
    pal::setThemes(FELTS, pal::THEME_COUNT);
    pal::init();
    screens::begin();
}

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
