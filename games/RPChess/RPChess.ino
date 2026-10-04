// CHChess - isometric casino chess for the RPGame handheld (CH32X035,
// 128x128 ST7735, piezo). Rules and CPU: the ch2k engine from ArduChess by
// Peter Brown (tiberiusbrown), MPL-2.0 (ch2k.hpp).
//
// The files, by role:
//   rules and CPU  ch2k.hpp (the engine), Engine (the game's face on it),
//                  Match (turns, the CPU, verdicts, undo; no graphics)
//   screens        Screens (title, setup, play, options, stats, credits),
//                  Stage (the play screen: camera, gloves, pieces in motion, HUD)
//   drawing        Iso (the isometric board and the flat map), Fx (particles,
//                  banners); the art is generated, src/assets/
//   sound          Sounds
//   saving         Save
//   main loop      Frame; config.h holds the build switches
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
// The same frame runs from inside the CPU's search (see Frame.h), so the
// game keeps moving while the engine thinks.
#include <RPGame.h>
#include "config.h"
#include "Frame.h"

void setup() {
    rpgame.boot();
    dbg::begin("CHCS " CHCH_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    rpgame.setFrameRate(CHCH_FPS);
}

void loop() {
    frame::run(false);
}
