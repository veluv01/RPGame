// The effect tables (the soft ones last, as in Sfx), the title's tune and playSong().
#include "Sounds.h"

AUDIO_STEPS(CURSOR)  = { AUDIO_STEP(2100, 0, 10) };
AUDIO_STEPS(SELECT)  = { AUDIO_STEP(1700, 0, 18), AUDIO_STEP(2600, 0, 30) };
AUDIO_STEPS(DENY)    = { AUDIO_STEP(900, 650, 70) };
// A wooden piece set down: a knock and a higher tick.
AUDIO_STEPS(LAND)    = { AUDIO_STEP(2400, 1100, 14), AUDIO_REST(8), AUDIO_STEP(3300, 0, 16) };
AUDIO_STEPS(HOP)     = { AUDIO_STEP(1500, 3300, 80) };
// A smash - tones alternating high and low read as noise on a piezo, the
// lows lengthening as it lands - then the piece spinning away: falling
// swoops, about as long as it takes to fly off the screen.
AUDIO_STEPS(CAPTURE) = {
    AUDIO_STEP(3800, 0, 6), AUDIO_STEP(700, 0, 8), AUDIO_STEP(3200, 0, 6), AUDIO_STEP(600, 0, 8), AUDIO_STEP(2800, 0, 6), AUDIO_STEP(520, 0, 10),
    AUDIO_STEP(2400, 0, 6), AUDIO_STEP(480, 0, 12),
    AUDIO_STEP(2600, 2200, 70), AUDIO_STEP(2400, 2000, 70), AUDIO_STEP(2200, 1800, 70), AUDIO_STEP(2000, 1600, 70), AUDIO_STEP(1800, 1400, 70),
    AUDIO_STEP(1600, 1200, 80), AUDIO_STEP(1400, 900, 110) };
AUDIO_STEPS(COIN)    = { AUDIO_STEP(2800, 0, 10), AUDIO_STEP(3700, 0, 28) };
// A chip dropped on the pile: two bright clinks.
AUDIO_STEPS(CHIP)    = { AUDIO_STEP(3900, 0, 8), AUDIO_REST(14), AUDIO_STEP(3300, 0, 6), AUDIO_STEP(4100, 0, 14) };
AUDIO_STEPS(CROWN)   = {
    AUDIO_STEP(1568, 0, 46), AUDIO_STEP(2093, 0, 44), AUDIO_STEP(2637, 0, 46), AUDIO_STEP(3136, 0, 44), AUDIO_STEP(4186, 0, 60),   // 45s alternate 46/44: 2 ms steps
    AUDIO_STEP(3136, 0, 30), AUDIO_STEP(4186, 0, 30), AUDIO_STEP(3136, 0, 30), AUDIO_STEP(4186, 0, 120) };
AUDIO_STEPS(WHOOSH)  = { AUDIO_STEP(1200, 3800, 90) };
// CHBlackjack's BLACKJACK fanfare: the signature win.
AUDIO_STEPS(SWEEP)   = {
    AUDIO_STEP(1568, 0, 50), AUDIO_STEP(2093, 0, 50), AUDIO_STEP(2637, 0, 50), AUDIO_STEP(3136, 0, 90),
    AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40), AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40),
    AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40), AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40),
    AUDIO_STEP(2000, 4200, 220) };
// Victory: a short tune (was a Playtune score in CHBlackjack).
AUDIO_STEPS(WIN)     = {
    AUDIO_STEP(2093, 0, 110), AUDIO_STEP(2637, 0, 110), AUDIO_STEP(3136, 0, 110), AUDIO_STEP(4186, 0, 220), AUDIO_REST(60),
    AUDIO_STEP(3520, 0, 110), AUDIO_STEP(4186, 0, 330) };
AUDIO_STEPS(LOSE)    = { AUDIO_STEP(1568, 1480, 260), AUDIO_STEP(1480, 1397, 260), AUDIO_STEP(1397, 1319, 260), AUDIO_STEP(1319, 1209, 350), AUDIO_STEP(1209, 1100, 350) };
AUDIO_STEPS(DRAW)    = { AUDIO_STEP(1760, 0, 70), AUDIO_REST(40), AUDIO_STEP(1760, 0, 70), AUDIO_REST(40), AUDIO_STEP(1319, 0, 160) };
AUDIO_STEPS(TURN)    = { AUDIO_STEP(2637, 0, 40), AUDIO_STEP(3520, 0, 90) };
// The CPU's clock while it thinks: the faintest clicks (played soft).
AUDIO_STEPS(TICK)    = { AUDIO_STEP(1100, 0, 3) };
AUDIO_STEPS(TOCK)    = { AUDIO_STEP(850, 0, 3) };

const audio::Effect SOUNDS[(int)Sfx::COUNT] = {
    AUDIO_EFFECT(CURSOR, 0), AUDIO_EFFECT(SELECT, 1), AUDIO_EFFECT(DENY, 1), AUDIO_EFFECT(LAND, 1),
    AUDIO_EFFECT(HOP, 1), AUDIO_EFFECT(CAPTURE, 2), AUDIO_EFFECT(COIN, 1), AUDIO_EFFECT(CHIP, 1),
    AUDIO_EFFECT(CROWN, 3), AUDIO_EFFECT(WHOOSH, 1), AUDIO_EFFECT(SWEEP, 4), AUDIO_EFFECT(WIN, 4),
    AUDIO_EFFECT(LOSE, 4), AUDIO_EFFECT(DRAW, 4), AUDIO_EFFECT(TURN, 1), AUDIO_EFFECT(TICK, audio::SOFT),
    AUDIO_EFFECT(TOCK, audio::SOFT),
};

// The title's tune, a casino two-step of our own: (MIDI note, length in
// sixteenths of 105 ms) pairs, 0 a rest; each note lets go 25 ms early.
#define N(note, len) (uint8_t)(84 + (note)), len      // semitones above C6 (1046 Hz)
#define R(len) 0, len
static const uint8_t TUNE[] = {
    N(7, 2), N(12, 2), N(16, 2), N(12, 2),  N(19, 3), N(16, 1), N(12, 4),
    N(9, 2), N(12, 2), N(17, 2), N(12, 2),  N(21, 3), N(17, 1), N(14, 4),
    N(7, 2), N(12, 2), N(16, 2), N(12, 2),  N(19, 2), N(21, 2), N(19, 2), N(16, 2),
    N(14, 2), N(17, 2), N(16, 2), N(14, 2), N(12, 4), R(4),
    N(4, 2), N(7, 2), N(12, 4),             N(5, 2), N(9, 2), N(12, 4),
    N(7, 2), N(11, 2), N(14, 2), N(17, 2),  N(16, 4), R(4),
    N(16, 2), N(14, 2), N(12, 2), N(9, 2),  N(7, 2), N(9, 2), N(12, 4),
    N(14, 2), N(11, 2), N(7, 2), N(11, 2),  N(12, 6), R(10),
};
static const audio::Melody TITLE_TUNE = { TUNE, sizeof TUNE / 2, 105, 25 };

void playSong(Song s, bool loop) {
    if (s == Song::TITLE) audio::melody(TITLE_TUNE, loop);
    else audio::stopMusic();
}
