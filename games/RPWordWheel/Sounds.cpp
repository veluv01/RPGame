#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")
// The effect tables (the soft ones last, as in Sfx) and playSong().
#include "Sounds.h"
#include "src/audio/Music.h"

AUDIO_STEPS(CURSOR)    = { AUDIO_STEP(2100, 0, 10) };
AUDIO_STEPS(SELECT)    = { AUDIO_STEP(1700, 0, 18), AUDIO_STEP(2600, 0, 30) };
AUDIO_STEPS(DENY)      = { AUDIO_STEP(900, 650, 70) };
AUDIO_STEPS(WHOOSH)    = { AUDIO_STEP(1200, 3800, 90) };
AUDIO_STEPS(COIN)      = { AUDIO_STEP(2800, 0, 10), AUDIO_STEP(3700, 0, 28) };
// A letter that is not there: the flat double buzz.
AUDIO_STEPS(BUZZER)    = { AUDIO_STEP(760, 0, 34), AUDIO_STEP(640, 0, 34), AUDIO_STEP(760, 0, 34), AUDIO_STEP(640, 0, 34), AUDIO_STEP(760, 0, 34),
                                         AUDIO_STEP(640, 0, 120) };
// The slide whistle down (one 620 ms sweep, in two halves: a step lasts at
// most 510 ms), and the thud at the bottom.
AUDIO_STEPS(BANKRUPT)  = { AUDIO_STEP(3400, 2100, 310), AUDIO_STEP(2100, 800, 310), AUDIO_REST(50), AUDIO_STEP(700, 600, 90) };
AUDIO_STEPS(LOSETURN)  = { AUDIO_STEP(1500, 1100, 120), AUDIO_STEP(1100, 800, 220) };
AUDIO_STEPS(BUZZIN)    = { AUDIO_STEP(2600, 0, 40), AUDIO_REST(20), AUDIO_STEP(2600, 0, 110) };
// The final spin's bell: four strokes.
AUDIO_STEPS(BELL)      = { AUDIO_STEP(3520, 0, 70), AUDIO_REST(70), AUDIO_STEP(3520, 0, 70), AUDIO_REST(70), AUDIO_STEP(3520, 0, 70), AUDIO_REST(70),
                                         AUDIO_STEP(3520, 0, 160) };
// The audience, as a wheel creeps past BANKRUPT.
AUDIO_STEPS(OOH)       = { AUDIO_STEP(1900, 1700, 70), AUDIO_STEP(1750, 1550, 70), AUDIO_STEP(1600, 1400, 70), AUDIO_STEP(1450, 1200, 160) };
AUDIO_STEPS(SOLVE)     = { AUDIO_STEP(2093, 0, 60), AUDIO_STEP(2637, 0, 60), AUDIO_STEP(3136, 0, 60), AUDIO_STEP(4186, 0, 170) };
AUDIO_STEPS(BIGWIN)    = {
    AUDIO_STEP(1568, 0, 50), AUDIO_STEP(2093, 0, 50), AUDIO_STEP(2637, 0, 50), AUDIO_STEP(3136, 0, 90),
    AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40), AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40),
    AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40), AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40),
    AUDIO_STEP(2000, 4200, 220) };
AUDIO_STEPS(ENVELOPE)  = { AUDIO_STEP(2000, 3000, 60), AUDIO_STEP(3000, 0, 70) };
AUDIO_STEPS(FLIP)      = { AUDIO_STEP(1200, 3000, 130) };
AUDIO_STEPS(TIMEUP)    = { AUDIO_STEP(900, 0, 120), AUDIO_REST(40), AUDIO_STEP(700, 0, 320) };
AUDIO_STEPS(LOSE)      = { AUDIO_STEP(1300, 950, 140), AUDIO_STEP(950, 700, 220) };
AUDIO_STEPS(TICK)      = { AUDIO_STEP(1100, 0, 3) };
AUDIO_STEPS(TOCK)      = { AUDIO_STEP(850, 0, 3) };

const audio::Effect SOUNDS[(int)Sfx::COUNT] = {
    AUDIO_EFFECT(CURSOR, 0), AUDIO_EFFECT(SELECT, 1), AUDIO_EFFECT(DENY, 1), AUDIO_EFFECT(WHOOSH, 1),
    AUDIO_EFFECT(COIN, 1), AUDIO_EFFECT(BUZZER, 3), AUDIO_EFFECT(BANKRUPT, 4), AUDIO_EFFECT(LOSETURN, 3),
    AUDIO_EFFECT(BUZZIN, 3), AUDIO_EFFECT(BELL, 4), AUDIO_EFFECT(OOH, 2), AUDIO_EFFECT(SOLVE, 3),
    AUDIO_EFFECT(BIGWIN, 4), AUDIO_EFFECT(ENVELOPE, 2), AUDIO_EFFECT(FLIP, 2), AUDIO_EFFECT(TIMEUP, 3),
    AUDIO_EFFECT(LOSE, 3), AUDIO_EFFECT(TICK, audio::SOFT), AUDIO_EFFECT(TOCK, audio::SOFT),
};

void playSong(Song s, bool loop) {
    const uint8_t *data; size_t n;
    music::get((uint8_t)s, loop, data, n);
    audio::music(data, loop);
}
