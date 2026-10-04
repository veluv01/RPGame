// One frame of the game (Frame.h), from loop() or from inside the CPU's
// search, and how often the search lets one through.
#pragma GCC optimize("Os")
#include <Arduino.h>
#include <RPGame.h>
#include "config.h"
#include "Frame.h"
#include "Screens.h"
#include "Stage.h"
#include "Engine.h"
#include "Sounds.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace frame {

// While the CPU thinks, the search runs flat out - no frames at all - and
// every SEARCH_MS stops for a BURST_MS burst of frames at the full rate: the
// CPU's glove glides to the piece it is weighing (stage::thinkPick). On the
// board a flush alone costs the search ~2.6 ms of CPU (~5 ms before RPGfx
// 1.3) and a redraw ~8 more, so short smooth bursts beat drawing thinly all
// along. A button starts a burst at once, and a held one or an open menu
// keeps it going. The engine calls back every 8 nodes (CH2K_POLL_NODES,
// ~5 ms), so that is prompt. Between bursts, a frame every BOB_MS keeps the
// glove bobbing (a step of it each), and a soft clock ticks and tocks every
// TICK_MS: slowly, so time seems to slow down (and the search's hiccups hide
// between the beats).
static const uint16_t FIRST_MS = 300, SEARCH_MS = 2000, BURST_MS = 450, BOB_MS = 133, TICK_MS = 2000;
static uint32_t burstAt, bobAt, tickAt;
static bool tock;
// Lockstep (scripts, the simulator): one frame per two polls, i.e. per 16
// nodes, so a scripted CPU move always takes the same frames.
static const uint8_t POLLS_PER_FRAME = 2;
#ifdef CHSIM
// Free-running simulator: the board's search speed (~1,700 nodes/s,
// measured), so thinking takes as long as there.
static const uint32_t SIM_US_PER_POLL = 8 * 590;
#endif

// Frames drawn from inside the search run on a stack of their own: the
// search can be ~1.5 KB deep in the 2 KB stack, and a frame (drawing, the
// debug protocol, interrupts) needs about 800 bytes more.
#ifndef CHSIM
static uint32_t frameStack[256] __attribute__((aligned(16)));   // 1 KB

__attribute__((noinline)) static void onFrameStack(void (*fn)()) {
    asm volatile(
        "mv   t1, sp\n"
        "mv   sp, %1\n"
        "addi sp, sp, -16\n"
        "sw   t1, 12(sp)\n"
        "jalr ra, 0(%0)\n"
        "lw   t1, 12(sp)\n"
        "mv   sp, t1\n"
        :
        : "r"(fn), "r"(frameStack + 256)
        : "ra", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
          "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "memory");
}
#else
static void onFrameStack(void (*fn)()) { fn(); }
#endif

void begin() {
#ifndef CHSIM
    dbg::frameStack(frameStack, frameStack + 256);   // P's fstk= (CHGAME_DEBUG builds)
#endif
    audio::begin(SOUNDS, (uint8_t)Sfx::COUNT, false);   // on once the options are read
    pal::init();
    screens::begin();
    eng::pollHook = thinkPoll;
}

bool run(bool thinking) {
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
        screens::update(thinking);
    } while (++ticks < 3 && rpgame.nextFrame());
    gfx_wait();
    pal::commit();
    dbg::markRenderStart();
    screens::render(rpgame.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
    return true;
}

static void thinkFrame();

void thinkPoll() { onFrameStack(thinkFrame); }

static void thinkFrame() {
#if CHGAME_DEBUG
    if (rpgame.lockstep >= 0) {
        static uint8_t polls;
        dbg::poll();
        if (++polls < POLLS_PER_FRAME) return;
        polls = 0;
        // Out of frames: wait for the driver's next N, as loop() would.
        for (;;) {
            dbg::poll();
            if (rpgame.lockstep != 0) break;
            dbg::waitInput();
        }
        run(true);
        return;
    }
#endif
#ifdef CHSIM
    sim_advance(SIM_US_PER_POLL);
#endif
    uint32_t now = millis();
    if (eng::nodes() <= 8) burstAt = now + FIRST_MS;        // a new search
    if ((int32_t)(now - tickAt) >= 0) {
        tickAt = now + TICK_MS;
        audio::sfx((tock = !tock) ? Sfx::Tock : Sfx::Tick);
    }
    if ((int32_t)(now - burstAt) < 0 && !(rpgame_readButtons() | rpgame.injected)) {
        if ((int32_t)(now - bobAt) >= 0) {
            bobAt = now + BOB_MS;
            rpgame.frameCount |= 7;                 // frame >> 3 (the bob) steps on by one
            run(true);
        }
        return;
    }
    stage::thinkPick();
    uint32_t end = now + BURST_MS;
    while ((int32_t)(millis() - end) < 0 || (rpgame_readButtons() | rpgame.injected) || screens::holdFrames()) {
        if (!run(true)) {
#ifdef CHSIM
            sim_advance(1000);
#endif
        }
    }
    burstAt = millis() + SEARCH_MS;
}

}  // namespace frame
