// The effects and the tunes (Sounds.h). A step is AUDIO_STEP(Hz, the Hz it
// sweeps to or 0, ms); the number after each effect in SOUNDS is its priority.
#include "Sounds.h"

static const audio::Step CURSOR[]    = { AUDIO_STEP(2100, 0, 10) };
static const audio::Step SELECT[]    = { AUDIO_STEP(1700, 0, 18), AUDIO_STEP(2600, 0, 30) };
static const audio::Step DENY[]      = { AUDIO_STEP(900, 650, 70) };
static const audio::Step CHIP[]      = { AUDIO_STEP(3100, 0, 12), AUDIO_REST(9), AUDIO_STEP(3700, 0, 26) };
static const audio::Step LEVER[]     = { AUDIO_STEP(900, 1500, 40), AUDIO_REST(10), AUDIO_STEP(2600, 0, 8), AUDIO_REST(6), AUDIO_STEP(1900, 0, 10) };
static const audio::Step THUNK[]     = { AUDIO_STEP(1500, 1000, 16), AUDIO_STEP(2400, 0, 5) };
static const audio::Step ANTIC[]     = { AUDIO_STEP(1400, 2800, 160) };
static const audio::Step LOCK[]      = { AUDIO_STEP(3300, 0, 8), AUDIO_REST(4), AUDIO_STEP(2200, 0, 8), AUDIO_REST(4), AUDIO_STEP(3900, 0, 30) };
static const audio::Step COIN[]      = { AUDIO_STEP(2800, 0, 10), AUDIO_STEP(3700, 0, 28) };
static const audio::Step WIN[]       = { AUDIO_STEP(2093, 0, 60), AUDIO_STEP(2637, 0, 60), AUDIO_STEP(3136, 0, 60), AUDIO_STEP(4186, 0, 170) };
static const audio::Step BIGWIN[]    = {
    AUDIO_STEP(1568, 0, 50), AUDIO_STEP(2093, 0, 50), AUDIO_STEP(2637, 0, 50), AUDIO_STEP(3136, 0, 90),
    AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40), AUDIO_STEP(2093, 0, 40), AUDIO_STEP(2637, 0, 40),
    AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40), AUDIO_STEP(3136, 0, 40), AUDIO_STEP(4186, 0, 40),
    AUDIO_STEP(2000, 4200, 220) };
static const audio::Step JACKPOT[]   = {
    AUDIO_STEP(2093, 0, 70), AUDIO_STEP(2637, 0, 70), AUDIO_STEP(3136, 0, 70), AUDIO_STEP(4186, 0, 140), AUDIO_REST(40),
    AUDIO_STEP(3136, 0, 70), AUDIO_STEP(4186, 0, 260), AUDIO_REST(40),
    AUDIO_STEP(2093, 4186, 90), AUDIO_STEP(2093, 4186, 90), AUDIO_STEP(2093, 4186, 90), AUDIO_STEP(2093, 4186, 90),
    AUDIO_STEP(4186, 0, 60), AUDIO_STEP(3520, 0, 60), AUDIO_STEP(4186, 0, 60), AUDIO_STEP(3520, 0, 60), AUDIO_STEP(4186, 0, 400) };
static const audio::Step GONG[]      = { AUDIO_STEP(1175, 0, 30), AUDIO_STEP(1568, 1175, 120), AUDIO_STEP(1175, 1100, 380) };
static const audio::Step ROAR[]      = { AUDIO_STEP(700, 1600, 70), AUDIO_STEP(1600, 800, 90), AUDIO_STEP(800, 2400, 60), AUDIO_STEP(2400, 900, 140) };
static const audio::Step BROKE[]     = { AUDIO_STEP(1568, 1480, 300), AUDIO_STEP(1480, 1397, 300), AUDIO_STEP(1397, 1319, 300), AUDIO_STEP(1319, 1249, 400), AUDIO_STEP(1249, 1180, 400) };

const audio::Effect SOUNDS[(int)Sfx::COUNT] = {
    AUDIO_EFFECT(CURSOR, 0), AUDIO_EFFECT(SELECT, 1), AUDIO_EFFECT(DENY, 1), AUDIO_EFFECT(CHIP, 1),
    AUDIO_EFFECT(LEVER, 2), AUDIO_EFFECT(THUNK, 1), AUDIO_EFFECT(ANTIC, 2), AUDIO_EFFECT(LOCK, 2),
    AUDIO_EFFECT(COIN, 1), AUDIO_EFFECT(WIN, 3), AUDIO_EFFECT(BIGWIN, 4), AUDIO_EFFECT(JACKPOT, 5),
    AUDIO_EFFECT(GONG, 3), AUDIO_EFFECT(ROAR, 3), AUDIO_EFFECT(BROKE, 4),
};

// --- Music: (note, length) pairs, a length in units of the song's tempo, R_
// a rest; each note is let go 14 ms early so repeats stay apart. All three
// tunes are new for this game.
#define R_ 0
enum : uint8_t { nG5 = 79, nA5 = 81, nC6 = 84, nD6 = 86, nE6 = 88, nF6 = 89, nG6 = 91, nA6 = 93, nB6 = 95,
                 nC7 = 96, nD7 = 98, nE7 = 100 };     // MIDI notes
// Title: a bouncing rag in C.
static const uint8_t TITLE[] = {
    nE6,1, nG6,1, nC7,2, nG6,1, nE6,1, nG6,2,   nF6,1, nA6,1, nC7,2, nA6,1, nF6,1, nA6,2,
    nG6,1, nB6,1, nD7,2, nB6,1, nG6,1, nD7,2,   nC7,2, nG6,2, nE6,2, R_,2,
    nE6,1, nG6,1, nC7,2, nE7,1, nC7,1, nG6,2,   nF6,1, nA6,1, nC7,2, nD7,1, nC7,1, nA6,2,
    nG6,1, nG6,1, nB6,2, nD7,1, nB6,1, nG6,2,   nC7,3, R_,1, nC7,2, R_,2,
};
// Free games: a pentatonic run that keeps climbing.
static const uint8_t FREE[] = {
    nA6,1, nG6,1, nE6,2, nD6,1, nE6,1, nG6,2,   nA6,1, nC7,1, nA6,2, nG6,1, nE6,1, nD6,2,
    nE6,1, nG6,1, nA6,2, nG6,1, nE6,1, nD6,1, nC6,1,   nD6,2, nE6,2, nC6,2, R_,2,
};
// The bonus wheel: a fairground arpeggio.
static const uint8_t WHEEL[] = {
    nC6,1, nE6,1, nG6,1, nC7,1, nG6,1, nE6,1,   nD6,1, nF6,1, nA6,1, nD7,1, nA6,1, nF6,1,
    nE6,1, nG6,1, nB6,1, nE7,1, nB6,1, nG6,1,   nD6,1, nG6,1, nB6,1, nD7,1, nB6,1, nG6,1,
};
#define SONG(a, ms) { a, (uint8_t)(sizeof(a) / 2), ms, 14 }
static const audio::Melody SONGS[(int)Song::COUNT - 1] = { SONG(TITLE, 105), SONG(FREE, 95), SONG(WHEEL, 80) };

static Song current = Song::None;
static bool looping = true, musicOn = true;

static void start() { audio::melody(SONGS[(int)current - 1], looping); }

void playSong(Song s, bool loop) {
    if (s == current) return;
    current = s; looping = loop;
    if (s == Song::None) audio::stopMusic();
    else start();
}

void setMusicOn(bool on) {
    if (on == musicOn) return;
    musicOn = on;
    audio::setMusic(on ? audio::ARPEGGIO : audio::MUSIC_OFF);     // off holds the tune where it is
    if (on && current != Song::None) start();                       // so start it over
}
