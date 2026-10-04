// CHDominoes - casino dominoes for the RPGame handheld (CH32X035,
// 128x128 ST7735, piezo), in the look of CHBlackjack and CHChess: ALL FIVES
// and DRAW with the double-six set, against the CPU or between two players.
//
// The files, by role:
//   rules and CPU     Dominoes (the two games), Ai, Match (a match's flow)
//   the table         Layout (where tiles lie), Table (felt and tiles drawn)
//   screens           Frame (one frame), Screens (title, setup, play, options),
//                     Stage (the play screen in motion, the firecrackers)
//   drawing and sound Colours, Font, Fx (sparkle), Sounds
//   saving            Save
//   generated         src/assets (tools/assets.py: the glove, lettering, logo)
#include <RPGame.h>
#include "config.h"
#include "Frame.h"

void setup() {
    rpgame.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    rpgame.setFrameRate(CHDM_FPS);
}

void loop() {
    frame::run();
}
