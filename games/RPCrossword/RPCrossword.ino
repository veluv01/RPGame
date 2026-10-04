// CHCrossword - a crossword for the RPGame handheld (CH32X035, 128x128
// ST7735, piezo), in the look of CHBlackjack and the casino games after it.
//
// The files:
//   Frame.*        one frame: buttons, logic ticks, drawing, the DMA flush
//   Screens.*      title, setup, play (pause, result), options; debug hooks
//   Game.*         the rules: letters, words that lock, score, the clock
//   Puzzle.*       a puzzle decoded from its packed bytes
//   Pack.*         where puzzles come from: flash, or packs on the SD card
//   Stage.*        the play screen: grid, clue, letter board, close-up
//   Font.*, Fx.*   the display font; particles and banners
//   Sounds.*       the sound effects
//   Save.*         what a save holds
//   src/           generated: the art (tools/assets.py) and the built-in
//                  puzzles (tools/puzzles/build_pack.py)
#include <RPGame.h>
#include "config.h"

// Built for Tools > USB > Upload only: with USB Serial compiled in, the game
// is about 100 B over the flash. A debug build keeps Serial for the debug
// protocol and makes room with CHCW_LEAN instead.
#if defined(USE_CHGAME_USB_CDC) && !CHGAME_DEBUG
#error "CHCrossword needs Tools > USB > Upload only to fit in the flash (and Tools > Optimize > Smallest + LTO, the default)"
#endif
#include "Frame.h"

void setup() {
    rpgame.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    rpgame.setFrameRate(CHCW_FPS);
}

void loop() {
    frame::run();
}
