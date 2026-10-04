// A game (Game.h): the bag, drawing tiles, the moves, the end and the
// game packed for saving.
#pragma GCC optimize("Os", "no-ipa-sra", "no-caller-saves")
#include <string.h>
#include "Game.h"
#include "Dict.h"

namespace game {

Setup setup;
uint8_t board[wd::CELLS];
uint8_t rack[2][wd::RACK];
int16_t score[2];
uint8_t bagLeft;
uint8_t turn;
bool over;
int16_t rackPenalty[2];
Last last;

static uint8_t bag[28];             // tiles of each kind still in the bag
static uint8_t scoreless;
static uint32_t rng;

static uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint8_t draw() {
    uint32_t k = rnd() % bagLeft;
    uint8_t t = 1;
    while (k >= bag[t]) k -= bag[t++];
    bag[t]--;
    bagLeft--;
    return t;
}

static void refill(uint8_t side) {
    for (uint8_t i = 0; i < wd::RACK && bagLeft; i++)
        if (!rack[side][i]) rack[side][i] = draw();
}

uint8_t tilesIn(uint8_t side) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < wd::RACK; i++) n += rack[side][i] != 0;
    return n;
}

static int16_t rackValue(uint8_t side) {
    int16_t v = 0;
    for (uint8_t i = 0; i < wd::RACK; i++) v = (int16_t)(v + wd::VALUE[rack[side][i]]);
    return v;
}

void start(const Setup &s) {
    setup = s;
    memset(board, 0, sizeof board);
    memset(rack, 0, sizeof rack);
    memcpy(bag, wd::COUNT, sizeof bag);
    bagLeft = 100;
    score[0] = score[1] = 0;
    rackPenalty[0] = rackPenalty[1] = 0;
    turn = 0;
    scoreless = 0;
    over = false;
    last.kind = NOTHING;
    rng = s.seed ? s.seed : 1;
    refill(0);
    refill(1);
}

bool wordsOk(const Play &pl, const wd::Result &r, bool coreOnly, wd::Span &bad) {
    uint8_t w[wd::SIZE];
    for (uint8_t i = 0; i < r.nWords; i++) {
        wd::letters(board, pl.p, pl.n, r.word[i], w);
        bool ok = coreOnly ? dict::hasCore(w, r.word[i].len) : dict::has(w, r.word[i].len);
        if (!ok) { bad = r.word[i]; return false; }
    }
    return true;
}

// The turn is done: is the game?
static void endTurn(bool scored) {
    uint8_t side = turn;
    scoreless = scored ? 0 : (uint8_t)(scoreless + 1);
    bool out = !bagLeft && !tilesIn(side);
    if (out) {
        // Going out wins what the other rack holds.
        int16_t v = rackValue(side ^ 1);
        rackPenalty[side] = v;
        rackPenalty[side ^ 1] = (int16_t)-v;
    } else if (scoreless >= SCORELESS_END) {
        rackPenalty[0] = (int16_t)-rackValue(0);
        rackPenalty[1] = (int16_t)-rackValue(1);
    } else {
        turn ^= 1;
        return;
    }
    score[0] = (int16_t)(score[0] + rackPenalty[0]);
    score[1] = (int16_t)(score[1] + rackPenalty[1]);
    over = true;
}

void play(const Play &pl, const wd::Result &r) {
    uint8_t side = turn;
    last.kind = PLAYED;
    last.side = side;
    last.n = pl.n;
    last.score = r.score;
    last.main = r.word[0];
    for (uint8_t i = 0; i < pl.n; i++) {
        uint8_t t = wd::tileOf(pl.p[i].tile);
        for (uint8_t k = 0; k < wd::RACK; k++)
            if (rack[side][k] == t) { rack[side][k] = 0; break; }
        board[pl.p[i].cell] = pl.p[i].tile;
        last.cell[i] = pl.p[i].cell;
    }
    score[side] = (int16_t)(score[side] + r.score);
    refill(side);
    endTurn(true);
}

void swap(uint8_t slots) {
    uint8_t side = turn, back[wd::RACK], n = 0;
    for (uint8_t i = 0; i < wd::RACK; i++)
        if ((slots >> i & 1) && rack[side][i]) { back[n++] = rack[side][i]; rack[side][i] = 0; }
    refill(side);                       // draw first, then the old tiles go in
    for (uint8_t i = 0; i < n; i++) { bag[back[i]]++; bagLeft++; }
    last.kind = SWAPPED;
    last.side = side;
    last.n = n;
    last.score = 0;
    endTurn(false);
}

void pass() {
    last.kind = PASSED;
    last.side = turn;
    last.n = 0;
    last.score = 0;
    endTurn(false);
}

uint8_t winner() { return score[0] == score[1] ? 2 : score[1] > score[0]; }

// ---------------------------------------------------------------------------
// Saving: the squares at 5 bits each (blanks listed apart); the bag is what
// is left of the hundred tiles.
// ---------------------------------------------------------------------------
void save(Record &r) {
    memset(&r, 0, sizeof r);
    r.blank[0] = r.blank[1] = 0xFF;
    uint8_t nb = 0;
    for (uint8_t c = 0; c < wd::CELLS; c++) {
        uint16_t bit = (uint16_t)(c * 5);
        uint16_t v = (uint16_t)((board[c] & wd::LETTER) << (bit & 7));
        r.cells[bit >> 3] |= (uint8_t)v;
        r.cells[(bit >> 3) + 1] |= (uint8_t)(v >> 8);
        if ((board[c] & wd::BLANK) && nb < 2) r.blank[nb++] = c;
    }
    memcpy(r.rack, rack, sizeof rack);
    r.score[0] = score[0];
    r.score[1] = score[1];
    r.rng = rng;
    r.turn = turn;
    r.scoreless = scoreless;
    r.mode = setup.mode;
    r.level = setup.level;
}

bool load(const Record &r) {
    memcpy(bag, wd::COUNT, sizeof bag);
    uint8_t left = 100;
    bool good = true;
    auto take = [&](uint8_t t) {
        if (t > wd::BLANK_TILE || !bag[t]) { good = false; return; }
        bag[t]--;
        left--;
    };
    for (uint8_t c = 0; c < wd::CELLS; c++) {
        uint16_t bit = (uint16_t)(c * 5);
        uint16_t v = (uint16_t)(r.cells[bit >> 3] | r.cells[(bit >> 3) + 1] << 8);
        uint8_t t = (uint8_t)((v >> (bit & 7)) & wd::LETTER);
        if (t > 26) return false;
        bool blank = t && (r.blank[0] == c || r.blank[1] == c);
        board[c] = blank ? (uint8_t)(t | wd::BLANK) : t;
        if (t) take(blank ? wd::BLANK_TILE : t);
    }
    for (uint8_t s = 0; s < 2; s++)
        for (uint8_t i = 0; i < wd::RACK; i++)
            if (r.rack[s][i]) take(r.rack[s][i]);
    if (!good || r.turn > 1 || r.mode > TWO_PLAYER || r.level >= LEVELS) return false;
    memcpy(rack, r.rack, sizeof rack);
    bagLeft = left;
    score[0] = r.score[0];
    score[1] = r.score[1];
    rng = r.rng ? r.rng : 1;
    turn = r.turn;
    scoreless = r.scoreless;
    setup.mode = r.mode;
    setup.level = r.level;
    setup.seed = r.rng;
    rackPenalty[0] = rackPenalty[1] = 0;
    over = false;
    last.kind = NOTHING;
    return true;
}

}  // namespace game
