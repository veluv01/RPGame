// CHWordWheel - WORD WHEEL, a word-puzzle game show for the RPGame handheld
// (CH32X035, 128x128 ST7735, piezo), in the casino style of CHBlackjack and
// its siblings: spin the wheel, call a consonant, buy a vowel, solve the
// puzzle. Three podiums (you and two CPU contestants, or friends passing
// the handheld), toss-ups, a final spin and a bonus round, hosted by the
// dealer from CHBlackjack's tables.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
//
// The files, by role:
//   rules        Show (the episode, turns, money), Puzzle, Wedges, Spin, Cpu: pure logic
//   puzzles      Bank.h, FlashBank (the built-in bank), SdBank (PHRASES.BNK on a card)
//   screens      Screens: title, setup, play, options, stats, the end of an episode
//   presenting   Presenter (the rules' events become motion and sound), Fx (sparkle)
//   drawing      Layout.h (every coordinate), Stage (wall, host, letter rack), BoardView
//                (board, podiums, picker), WheelStrip (the wheel), Shapes
//   sound        Sounds (the effects; the title tune is src/audio/Music.*)
//   saving       Save
//   generated    src/assets (art), src/audio/Music.*, src/bank/BankData.*: do not edit
//   switches     config.h
#include <RPGame.h>
#include "config.h"

// Built for Tools > USB > Upload only: with USB Serial compiled in, the game
// is about 100 B over the flash. A debug build keeps Serial for the debug
// protocol and makes room with CHWW_LEAN instead.
#if defined(USE_CHGAME_USB_CDC) && !CHGAME_DEBUG
#error "CHWordWheel needs Tools > USB > Upload only to fit in the flash (and Tools > Optimize > Smallest + LTO, the default)"
#endif
#include "Screens.h"
#include "Save.h"

#if CHGAME_DEBUG
#ifdef CHSIM
#include <string.h>
void sim_cardEject(bool out);
#endif

// Game commands for the debug protocol (tools/chsim/chdrive.py 'say').
//   R <seed>          reseed the rules' generator and the deal of puzzles
//   F <n[,n...]>      the next spins' stops (0..71: wedge * 3 + peg slot)
//   U <section> <i>   the next puzzle of that section (0 round, 1 toss-up, 2 bonus)
//   C <letter>        call a letter, as if picked
//   V <1|0>           the solve in progress comes out right or wrong
//   M <player> <cash> set a podium's round money
//   G <step>          jump the episode to a step (toss-up, round, ...)
//   W <k0> <k1> <k2>  who is at the podiums (0 human, 1 Ace, 2 Dot, 3 Buzz)
//   J <T|U|P|E|O|S>   jump to a screen
//   H                 one line of game state ("ST phase=...")
//   E <1|0>           let a debug build on the board write its save pages
//   X <1|0>           (simulator) put the SD card in, or pull it out
static bool debugHook(char cmd, const char *args) {
    const char *p = args;
    switch (cmd) {
        case 'R': screens::debugSeed(dbg::parseNum(args, 10)); return true;
        case 'F':
            while (*p) {
                screens::debugStop((uint8_t)dbg::parseNum(p, 10));
                while (*p == ' ' || *p == ',') p++;
                if (*p && (*p < '0' || *p > '9')) break;
            }
            return true;
        case 'U': {
            uint8_t section = (uint8_t)dbg::parseNum(p, 10);
            screens::debugPuzzle(section, (uint16_t)dbg::parseNum(p, 10));
            return true;
        }
        case 'C':
            while (*p == ' ') p++;
            screens::debugCall(*p);
            return true;
        case 'V': screens::debugSolve(dbg::parseNum(p, 10) != 0); return true;
        case 'M': {
            uint8_t who = (uint8_t)dbg::parseNum(p, 10);
            screens::debugCash(who, (int32_t)dbg::parseNum(p, 10));
            return true;
        }
        case 'G': screens::debugStep((uint8_t)dbg::parseNum(p, 10)); return true;
        case 'W': {
            uint8_t a = (uint8_t)dbg::parseNum(p, 10), b = (uint8_t)dbg::parseNum(p, 10);
            screens::debugKinds(a, b, (uint8_t)dbg::parseNum(p, 10));
            return true;
        }
        case 'J': screens::debugJump(args[0]); return true;
        case 'H': {
            char buf[120];
            screens::debugState(buf);
            dbg::print(buf);
            return true;
        }
        case 'E': save::allowWrites(args[0] == '1'); return true;
#ifdef CHSIM
        case 'X': sim_cardEject(args[0] != '1'); return true;
#endif
    }
    return false;
}
#endif

void setup() {
    rpgame.boot();
    dbg::begin("CHWW " CHWW_VERSION);     // the debug protocol's hello (CHGAME_DEBUG builds)
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    rpgame.setFrameRate(CHWW_FPS);
#if CHGAME_DEBUG
    dbg::hook = debugHook;
#endif
}

void loop() {
    dbg::poll();
    if (!rpgame.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the
    // wheel and the clocks never slow down.
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
