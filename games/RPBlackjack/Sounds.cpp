// The sound effects as step lists (frequency from, frequency to - 0 for
// a held note - and milliseconds), each with its priority in the table
// below; the songs; and the two switches that turn sound on and off.
#pragma GCC optimize("Os")
#include "Sounds.h"
#include "src/audio/Music.h"
#include "config.h"

AUDIO_STEPS(DEAL)      = { AUDIO_STEP(3600, 1500, 22) };
AUDIO_STEPS(FLIP)      = { AUDIO_STEP(2300, 0, 8), AUDIO_REST(5), AUDIO_STEP(3300, 0, 12) };
AUDIO_STEPS(CHIP)      = { AUDIO_STEP(3100, 0, 12), AUDIO_REST(9), AUDIO_STEP(3700, 0, 26) };
AUDIO_STEPS(CURSOR)    = { AUDIO_STEP(2100, 0, 10) };
AUDIO_STEPS(SELECT)    = { AUDIO_STEP(1700, 0, 18), AUDIO_STEP(2600, 0, 30) };
AUDIO_STEPS(DENY)      = { AUDIO_STEP(900, 650, 70) };
AUDIO_STEPS(WIN)       = { AUDIO_STEP(2093, 0, 60), AUDIO_STEP(2637, 0, 60), AUDIO_STEP(3136, 0, 60), AUDIO_STEP(4186, 0, 170) };
AUDIO_STEPS(BLACKJACK) = {
    AUDIO_STEP(1568, 0, 50), AUDIO_STEP(2093, 0, 50), AUDIO_STEP(2637, 0, 50), AUDIO_STEP(3136, 0, 90),
    AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40), AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40),
    AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40), AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40),
    AUDIO_STEP(2000, 4200, 220) };
AUDIO_STEPS(BUST)      = { AUDIO_STEP(1600, 950, 110), AUDIO_STEP(950, 560, 130), AUDIO_STEP(560, 330, 230) };
AUDIO_STEPS(PUSH)      = { AUDIO_STEP(1760, 0, 70), AUDIO_REST(40), AUDIO_STEP(1760, 0, 70) };
AUDIO_STEPS(LOSE)      = { AUDIO_STEP(1300, 950, 140), AUDIO_STEP(950, 700, 220) };
AUDIO_STEPS(PEEK)      = { AUDIO_STEP(1400, 0, 16) };
// The riffle: 7 ms ticks, here 8 and 6 ms in turn (steps count 2 ms) so
// the sweep still lands on time.
AUDIO_STEPS(SHUFFLE)   = {
    AUDIO_STEP(3000, 0, 8), AUDIO_REST(12), AUDIO_STEP(3400, 0, 6), AUDIO_REST(12), AUDIO_STEP(3100, 0, 8), AUDIO_REST(12), AUDIO_STEP(3500, 0, 6), AUDIO_REST(12),
    AUDIO_STEP(3000, 0, 8), AUDIO_REST(12), AUDIO_STEP(3400, 0, 6), AUDIO_REST(12), AUDIO_STEP(3200, 0, 8), AUDIO_REST(12), AUDIO_STEP(3600, 0, 6), AUDIO_REST(60),
    AUDIO_STEP(2400, 3800, 120) };
AUDIO_STEPS(COIN)      = { AUDIO_STEP(2800, 0, 10), AUDIO_STEP(3700, 0, 28) };
AUDIO_STEPS(SPLIT)     = { AUDIO_STEP(1500, 3100, 90), AUDIO_REST(20), AUDIO_STEP(3100, 0, 30) };
AUDIO_STEPS(DOUBLE)    = { AUDIO_STEP(2000, 0, 30), AUDIO_REST(20), AUDIO_STEP(2600, 0, 30), AUDIO_REST(20), AUDIO_STEP(3200, 0, 60) };
AUDIO_STEPS(INSURANCE) = { AUDIO_STEP(2637, 0, 60), AUDIO_STEP(3136, 0, 60), AUDIO_STEP(2637, 0, 60), AUDIO_STEP(3136, 0, 120) };
AUDIO_STEPS(BROKE)     = { AUDIO_STEP(1568, 1480, 300), AUDIO_STEP(1480, 1397, 300), AUDIO_STEP(1397, 1319, 300), AUDIO_STEP(1319, 1249, 400), AUDIO_STEP(1249, 1180, 400) };
AUDIO_STEPS(REVEAL)    = { AUDIO_STEP(1800, 3000, 60) };
AUDIO_STEPS(WHOOSH)    = { AUDIO_STEP(1200, 3800, 90) };

const audio::Effect SOUNDS[(int)Sfx::COUNT] = {
    AUDIO_EFFECT(DEAL, 1), AUDIO_EFFECT(FLIP, 1), AUDIO_EFFECT(CHIP, 1), AUDIO_EFFECT(CURSOR, 0),
    AUDIO_EFFECT(SELECT, 1), AUDIO_EFFECT(DENY, 1), AUDIO_EFFECT(WIN, 3), AUDIO_EFFECT(BLACKJACK, 4),
    AUDIO_EFFECT(BUST, 3), AUDIO_EFFECT(PUSH, 3), AUDIO_EFFECT(LOSE, 3), AUDIO_EFFECT(PEEK, 1),
    AUDIO_EFFECT(SHUFFLE, 2), AUDIO_EFFECT(COIN, 1), AUDIO_EFFECT(SPLIT, 2), AUDIO_EFFECT(DOUBLE, 2),
    AUDIO_EFFECT(INSURANCE, 3), AUDIO_EFFECT(BROKE, 4), AUDIO_EFFECT(REVEAL, 2), AUDIO_EFFECT(WHOOSH, 1),
};

void playSong(Song s, bool loop) {
#if CHGAME_DEBUG
    // Debug builds have no scores (src/audio/Music.cpp): with music() never called,
    // the library's score player stays out of the image too.
    (void)s; (void)loop;
#else
    const uint8_t *data; size_t n;
    music::get((uint8_t)s, loop, data, n);
    audio::music(data, loop);
#endif
}

namespace sound {

static uint8_t mode = 0;
static bool isMuted = false;

static void apply() { audio::setOn(mode && !isMuted); }

void setMode(uint8_t m) {
    mode = m;
    if (m) audio::setMusic(m == 2 ? audio::LEAD : audio::ARPEGGIO);
    apply();
}

void mute(bool m) { isMuted = m; apply(); }
bool muted() { return isMuted; }

}  // namespace sound
