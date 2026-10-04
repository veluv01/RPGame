// The effects and the music (Sounds.h). A step is AUDIO_STEP(Hz, the Hz it
// sweeps to or 0, ms); the number after each effect in SOUNDS is its priority.
#include "Sounds.h"
#include "src/audio/Music.h"
#include "config.h"

AUDIO_STEPS(CURSOR)    = { AUDIO_STEP(2100, 0, 10) };
AUDIO_STEPS(SELECT)    = { AUDIO_STEP(1700, 0, 18), AUDIO_STEP(2600, 0, 30) };
AUDIO_STEPS(DENY)      = { AUDIO_STEP(900, 650, 70) };
AUDIO_STEPS(CHIP)      = { AUDIO_STEP(3100, 0, 12), AUDIO_REST(9), AUDIO_STEP(3700, 0, 26) };
AUDIO_STEPS(COIN)      = { AUDIO_STEP(2800, 0, 10), AUDIO_STEP(3700, 0, 28) };
AUDIO_STEPS(WHOOSH)    = { AUDIO_STEP(1200, 3800, 90) };
AUDIO_STEPS(WIN)       = { AUDIO_STEP(2093, 0, 60), AUDIO_STEP(2637, 0, 60), AUDIO_STEP(3136, 0, 60), AUDIO_STEP(4186, 0, 170) };
AUDIO_STEPS(BIGWIN)    = {
    AUDIO_STEP(1568, 0, 50), AUDIO_STEP(2093, 0, 50), AUDIO_STEP(2637, 0, 50), AUDIO_STEP(3136, 0, 90),
    AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40), AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40),
    AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40), AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40),
    AUDIO_STEP(2000, 4200, 220) };
AUDIO_STEPS(LOSE)      = { AUDIO_STEP(1300, 950, 140), AUDIO_STEP(950, 700, 220) };
AUDIO_STEPS(BROKE)     = { AUDIO_STEP(1568, 1480, 300), AUDIO_STEP(1480, 1397, 300), AUDIO_STEP(1397, 1319, 300), AUDIO_STEP(1319, 1249, 400), AUDIO_STEP(1249, 1180, 400) };
AUDIO_STEPS(FLICK)     = { AUDIO_STEP(1500, 3300, 80) };                  // the croupier launches the ball
AUDIO_STEPS(CLACK)     = { AUDIO_STEP(3800, 0, 3), AUDIO_REST(4), AUDIO_STEP(2900, 0, 5), AUDIO_REST(3), AUDIO_STEP(3300, 0, 3) };   // a deflector
AUDIO_STEPS(THUNK)     = { AUDIO_STEP(1300, 700, 26), AUDIO_REST(18), AUDIO_STEP(2800, 0, 6) };   // into the pocket
AUDIO_STEPS(RAKE)      = { AUDIO_STEP(2600, 1100, 70) };                  // losing chips swept off
AUDIO_STEPS(TICK)      = { AUDIO_STEP(1100, 0, 3) };
AUDIO_STEPS(TOCK)      = { AUDIO_STEP(850, 0, 3) };

const audio::Effect SOUNDS[(int)Sfx::COUNT] = {
    AUDIO_EFFECT(CURSOR, 0), AUDIO_EFFECT(SELECT, 1), AUDIO_EFFECT(DENY, 1), AUDIO_EFFECT(CHIP, 1),
    AUDIO_EFFECT(COIN, 1), AUDIO_EFFECT(WHOOSH, 1), AUDIO_EFFECT(WIN, 3), AUDIO_EFFECT(BIGWIN, 4),
    AUDIO_EFFECT(LOSE, 3), AUDIO_EFFECT(BROKE, 4), AUDIO_EFFECT(FLICK, 1), AUDIO_EFFECT(CLACK, 1),
    AUDIO_EFFECT(THUNK, 2), AUDIO_EFFECT(RAKE, 1), AUDIO_EFFECT(TICK, audio::SOFT), AUDIO_EFFECT(TOCK, audio::SOFT),
};

void playSong(Song s, bool loop) {
#if CHGAME_DEBUG
    // Debug builds have no scores (Music.cpp): with music() never called,
    // the library's score player stays out of the image too.
    (void)s; (void)loop;
#else
    const uint8_t *data; size_t n;
    music::get((uint8_t)s, loop, data, n);
    audio::music(data, loop);
#endif
}
