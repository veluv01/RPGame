// One game at a table (Match.h): the start, turns, the cursor, the
// dealer's thinking, the clock and the payout.
#pragma GCC optimize("Os")
#include "Match.h"

const uint8_t ANTES[ANTE_COUNT] = {5, 10, 25, 50, 100, 250};
const int16_t GOALS[3] = {1000, 5000, 0};

void Match::emit(uint8_t type, uint8_t a, uint8_t b2) {
    if (nEv < sizeof ev / sizeof ev[0]) ev[nEv++] = {type, a, b2};
}

void Match::start(Mode m, uint8_t lvl, bool twoPlayers) {
    mode = m;
    level = lvl;
    two = twoPlayers;
    rules::start(b, m);
    first = 0;
    cur = (uint8_t)(b.w / 2 + b.w * (b.h / 2));          // the centre
    if (b.flags & F_GRAVITY) cur = (uint8_t)(b.w / 2);   // DROP 4: the cursor rides the top row
    if (b.flags & F_MINES)
        for (uint8_t i = 0; i < MINE_COUNT;) {
            uint32_t bit = 1u << (cpu::rnd(&rng) % b.n);
            if (!(b.mines & bit)) { b.mines |= bit; i++; }
        }
    size = 0;
    clock = BLITZ_TICKS;
    wins = boards = 0;
    result = R_NONE;
    forced = NONE;
    nEv = 0;
    emit(EV_START);
    to(Phase::Intro);
}

void Match::finish() {
    result = b.result;
    if (b.flags & F_BLITZ) result = wins >= 3 ? R_P0 : R_P1;
    b.seen = 0xFFFF;                                     // DARK: lights on
    if (two && result != R_DRAW) score[result - 1]++;
    emit(EV_OVER, result);
    to(Phase::Over);
}

void Match::nextTurn() {
    if (b.flags & F_COIN) {
        b.turn = (uint8_t)(cpu::rnd(&rng) >> 7 & 1);
        emit(EV_TOSS, b.turn);
        to(Phase::Toss);
    } else if (b.flags & F_AUCTION) {
        bidP = 0;
        to(Phase::Bid);
    } else goTurn();
}

void Match::goTurn() {
    if (b.turn == 0 || two) {
        shot = SHOT_TICKS;
        if ((b.flags & F_ULTIMATE) && b.must != NONE && smallOf(cur) != b.must) cur = smallCentre(b.must);
        if ((b.flags & F_GOBBLE) && !b.stock[b.turn][size])
            for (size = 0; size < 2 && !b.stock[b.turn][size]; size++) {}
        to(Phase::Human);
    } else {
        cpu::begin(b, level, &rng);
        thought = false;
        if (rules::legal(b, forced, 0)) { pend.cell = forced; pend.arg = 0; thought = true; }
        forced = NONE;
        to(Phase::Think);
    }
}

void Match::place(uint8_t cell, uint8_t arg) {
    uint8_t side = b.turn;
    rules::play(b, cell, arg);
    emit(b.boom ? EV_BOOM : EV_PLACE, cell, side);
    if (b.gone != NONE) emit(EV_GONE, b.gone);
    if (b.smallWon != NONE) emit(EV_SMALL, b.smallWon);
    if (b.result) {
        if (b.flags & F_BLITZ) {
            boards++;
            if (b.result == R_P0) wins++;
            else if (b.result == R_P1) clock = clock > 300 ? (uint16_t)(clock - 300) : 1;
            emit(EV_BOARD, b.result);
        }
    }
    to(Phase::Settle);
}

// The iso tables: the cursor goes to the nearest cell that way on screen
// (CHChess's glove). A cell (u, v) is at screen ((u - v) * 2, u + v), in
// half tile heights.
void Match::nudgeIso(uint8_t rep) {
    int dx = (rep & K_RIGHT) ? 1 : ((rep & K_LEFT) ? -1 : 0), dy = (rep & K_DOWN) ? 1 : ((rep & K_UP) ? -1 : 0);
    if (!dx && !dy) return;
    int u0 = cur % b.w, v0 = cur / b.w;
    uint8_t best = NONE;
    int bestScore = 0x7FFF;
    for (uint8_t c = 0; c < b.n; c++) {
        int u = c % b.w, v = c / b.w;
        int ex = 2 * ((u - v) - (u0 - v0)), ey = (u + v) - (u0 + v0);
        int along = dx ? ex * dx : ey * dy, perp = dx ? ey : ex;
        if (along <= 0) continue;
        int score = along + 3 * (perp < 0 ? -perp : perp);
        if (score < bestScore) { bestScore = score; best = c; }
    }
    if (best != NONE) { cur = best; emit(EV_MOVE); }
}

void Match::nudge(uint8_t rep) {
    if (iso) { nudgeIso(rep); return; }
    int x = cur % b.w, y = cur / b.w;
    if (rep & K_LEFT) x--;
    if (rep & K_RIGHT) x++;
    if (rep & K_UP) y--;
    if (rep & K_DOWN) y++;
    if (b.flags & F_GRAVITY) y = 0;
    if (x < 0 || x >= b.w || y < 0 || y >= b.h) return;
    uint8_t c = (uint8_t)(y * b.w + x);
    if (c != cur) { cur = c; emit(EV_MOVE); }
}

void Match::human(uint8_t pressed, uint8_t rep) {
    nudge(rep);
    uint8_t arg = 0;
    bool go = (pressed & K_A) != 0;
    if (b.flags & F_GOBBLE) {
        arg = size;
        if (pressed & K_B) {                              // the next size still in stock
            for (uint8_t i = 0; i < 3; i++) {
                size = (uint8_t)((size + 1) % 3);
                if (b.stock[b.turn][size]) break;
            }
            arg = size;
            emit(EV_ARG);
        }
    } else if ((b.flags & F_WILD) && (pressed & K_B)) { arg = 1; go = true; }
    if ((b.flags & F_BLITZ) && !--shot) {
        emit(EV_TIMEOUT);
        do cur = (uint8_t)(cpu::rnd(&rng) % b.n); while (!rules::legal(b, cur, 0));
        go = true;
    }
    if (!go) return;
    uint8_t cell = (b.flags & F_GRAVITY) ? rules::drop(b, cur) : cur;
    if ((b.flags & F_DARK) && cell < 9 && topOf(b.cell[cell]) == 2 && !(b.seen >> cell & 1)) {
        b.seen = (uint16_t)(b.seen | 1u << cell);        // walked into one: now you know
        emit(EV_BUMP, cell);
        return;
    }
    if (rules::legal(b, cell, arg)) place(cell, arg);
    else emit(EV_DENY);
}

void Match::update(uint8_t pressed, uint8_t rep, bool busy) {
    nEv = 0;
    if (t < 0xFFFF) t++;
    bool blitz = (b.flags & F_BLITZ) != 0;
    if (blitz && phase >= Phase::Human && phase <= Phase::Reach && clock && !--clock) { finish(); return; }

    switch (phase) {
        case Phase::Intro:
            if (!busy && t >= 30) { b.turn = first; nextTurn(); }
            break;
        case Phase::Toss:
            if (!busy && t >= 50) goTurn();
            break;
        case Phase::Bid: {
            uint8_t was = bidP;
            if ((rep & (K_UP | K_RIGHT)) && bidP < b.chips[0]) bidP++;
            if ((rep & (K_DOWN | K_LEFT)) && bidP > 0) bidP--;
            if (bidP != was) emit(EV_ARG);
            if (pressed & K_A) {
                bidC = cpu::bid(b, level, &rng);
                uint8_t w = bidP > bidC ? 0 : (bidC > bidP ? 1 : b.tieTo);
                if (bidP == bidC) b.tieTo ^= 1;
                uint8_t paid = w ? bidC : bidP;
                b.chips[w] = (uint8_t)(b.chips[w] - paid);
                b.chips[w ^ 1] = (uint8_t)(b.chips[w ^ 1] + paid);
                b.turn = w;
                emit(EV_BID, w);
                to(Phase::BidShow);
            }
            break;
        }
        case Phase::BidShow:
            if (!busy && t >= 70) goTurn();
            break;
        case Phase::Human:
            human(pressed, rep);
            break;
        case Phase::Think:
            if (!thought) thought = cpu::step(b, pend);
            if (thought && !busy && t >= (blitz ? 8 : 24)) { emit(EV_REACH, pend.cell); to(Phase::Reach); }
            break;
        case Phase::Reach:
            if (!busy && t >= 2) place(pend.cell, pend.arg);
            break;
        case Phase::Settle:
            if (busy || t < 8) break;
            if (!b.result) nextTurn();
            else if (blitz) to(Phase::Between);
            else finish();
            break;
        case Phase::Between:
            if (clock <= 1) { finish(); break; }
            if (t >= 40) {
                uint16_t keep = clock;
                first ^= 1;
                rules::start(b, mode);
                clock = keep;
                emit(EV_START);
                b.turn = first;
                goTurn();
            }
            break;
        case Phase::Over:
            break;
    }
}

int32_t Match::payout(int32_t ante, uint8_t streak) const {
    if (b.flags & F_BLITZ) return ante * wins * (wins + 1) / 8;
    if (result == R_DRAW) return ante;
    if (result != R_P0) return 0;
    const ModeDef &d = MODES[mode];
    int32_t win = ante * d.payNum / d.payDen * (level + 1);
    if (streak > 4) streak = 4;
    return ante + win + win * streak / 4;
}
